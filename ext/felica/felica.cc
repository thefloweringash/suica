#include <ruby.h>

#include <memory>
#include <iostream>
#include <exception>
#include <nfc/nfc.h>

template <typename T>
static void rb_mark_proxy(void *target) {
    reinterpret_cast<T*>(target)->rb_mark();
}

template <typename T>
static void delete_proxy(void *target) {
    delete reinterpret_cast<T*>(target);
}

template <typename T, void(*cleanup_func)(T*)>
struct cleanup_once {
    T* object;

    cleanup_once(T* object)
      : object(object)
    {}

    void cleanup() {
        if (this->object) {
            cleanup_func(this->object);
        }
        this->object = nullptr;
    }

    T* expose() {
        if (!this->object) {
            rb_raise(rb_eRuntimeError, "Exposing cleaned up object");
        }
        return this->object;
    }

    ~cleanup_once() {
        this->cleanup();
    }
};

using nfc_device_wrapper = cleanup_once<nfc_device, nfc_close>;

// the low level device
class felica_target {
    struct request_header {
        uint8_t len;
        uint8_t code;
        uint8_t idm[8];
    } __attribute__((packed));

    struct response_header {
        uint8_t len;
        uint8_t code;
        uint8_t idm[8];
    } __attribute__((packed));


    VALUE mNFCDevice;
    nfc_target mTarget;

    int transceiveRaw(const uint8_t *send, uint8_t sendLen,
                      uint8_t *recv, uint8_t recvLen,
                      int timeout = 300)
    {
        nfc_device_wrapper *wrapped_dev;
        Data_Get_Struct(mNFCDevice, nfc_device_wrapper, wrapped_dev);

        nfc_device *dev = wrapped_dev->expose();

        int res = nfc_initiator_transceive_bytes(
            dev,
            send, sendLen,
            recv, recvLen,
            timeout);
        return res;
    }

public:
    void rb_mark() {
        ::rb_gc_mark(mNFCDevice);
    }

    struct get_mode
    {
        constexpr static uint8_t code = 0x04;

        struct request {
            request_header header;
        } __attribute__((packed));

        struct response {
            response_header header;
            uint8_t mode;
        } __attribute__((packed));
    };

    template <int serviceListCount_, int blockListCount_, int blockListSize_>
    struct read_without_encryption {
        constexpr static uint8_t code = 0x06;

        struct request {
            request_header header;
            uint8_t serviceListCount;
            uint16_t serviceList[serviceListCount_];
            uint8_t blockListCount;
            uint8_t blockList[blockListSize_];

            request()
                : serviceListCount(serviceListCount_)
                , blockListCount(blockListCount_)
            {}
        } __attribute__((packed));

        struct response {
            response_header header;
            uint8_t statusFlag1;
            uint8_t statusFlag2;
            uint8_t blockCount;
            uint8_t blockData[16 * blockListCount_];
        } __attribute__((packed));
    };

    struct search_service {
        constexpr static uint8_t code = 0x0a;
        struct request {
            request_header header;
            uint16_t index;
        } __attribute__((packed));
        struct response {
            response_header header;
            uint8_t data[4];
        } __attribute__((packed));
    };

    felica_target(VALUE device,
                  const nfc_target& target)
        : mNFCDevice(device)
        , mTarget(target)
    {
    }

    template <typename T>
    int transceive(typename T::request* request,
                   typename T::response* response,
                   size_t requestLen = sizeof(typename T::request),
                   size_t responseLen = sizeof(typename T::response)) {
        static_assert(sizeof(typename T::response) < 256,
                      "response length fits inside uint8_t");
        request->header.len = requestLen;
        request->header.code = T::code;
        memcpy(&request->header.idm,
               mTarget.nti.nfi.abtId,
               sizeof(request->header.idm));
        return transceiveRaw(reinterpret_cast<const uint8_t*>(request),
                             requestLen,
                             reinterpret_cast<uint8_t*>(response),
                             responseLen);
    }
};

static VALUE cNFCContext;
static VALUE cNFCDevice;
static VALUE cFelicaTarget;

static VALUE cFelicaStatusError;
static ID idRaise;

/*
 * Initialise libnfc via +nfc_init+.
 * @api private
 * @return [Felica::NFC::Context]
 */
static VALUE nfc_make_context(VALUE klass) {
    nfc_context* p = nullptr;
    nfc_init(&p);

    if (!p)
        rb_raise(rb_eRuntimeError, "Failed to open nfc context");

    return Data_Wrap_Struct(cNFCContext, nullptr, nfc_exit, p);
}

/*
 * Opens an NFC reader via +nfc_open+.
 * @return [Felica::NFC::Device]
 */
static VALUE nfc_context_open_device_raw(VALUE self) {
    nfc_context *ctx;
    Data_Get_Struct(self, nfc_context, ctx);

    nfc_device *dev = nfc_open(ctx, NULL);
    if (!dev)
        rb_raise(rb_eRuntimeError, "Failed to open nfc device");

    nfc_device_wrapper *wrapped_dev = new nfc_device_wrapper(dev);

    return Data_Wrap_Struct(cNFCDevice,
                            nullptr,
                            delete_proxy<nfc_device_wrapper>,
                            wrapped_dev);
}

/*
 * Document-class: Felica::NFC::Device
 *
 * Wraps a libnfc +nfc_device+.
 */

/*
 * Initialize an +nfc_device+. Will configure the +nfc_device+ into
 * initiator mode.
 * @api private
 * @return [self]
 */
static VALUE nfc_device_init(VALUE self) {
    nfc_device_wrapper *wrapped_dev;
    Data_Get_Struct(self, nfc_device_wrapper, wrapped_dev);

    nfc_device *dev = wrapped_dev->expose();

    if (nfc_initiator_init(dev) < 0) {
        rb_raise(rb_eRuntimeError, "Failed to set initiator");
    }

    return self;
}

/*
 * Close an NFC reader via +nfc_close+.
 * @return [void]]
 */
static VALUE nfc_device_close(VALUE self) {
    nfc_device_wrapper *wrapped_dev;
    Data_Get_Struct(self, nfc_device_wrapper, wrapped_dev);

    wrapped_dev->cleanup();

    return Qnil;
}

/*
 * Search for a Felica and return a +Felica::Target+. Will block until a
 * Felica device is detected.
 * @return [Felica::Target]
 */
static VALUE nfc_device_select_felica(VALUE self) {
    nfc_device_wrapper *wrapped_dev;
    Data_Get_Struct(self, nfc_device_wrapper, wrapped_dev);

    nfc_device *dev = wrapped_dev->expose();

    nfc_modulation modulation;
    modulation.nmt = NMT_FELICA;
    modulation.nbr = NBR_212;

    nfc_target selected;

    int found_targets = nfc_initiator_select_passive_target(
        dev, modulation, NULL, 0, &selected);

    if (found_targets < 0) {
        rb_raise(rb_eRuntimeError, "Failed to select passive target");
    }

    if (found_targets == 0) {
        rb_raise(rb_eRuntimeError, "Missing target");
    }

    felica_target *selected_felica_target = new felica_target(self, selected);

    return Data_Wrap_Struct(cFelicaTarget,
                            rb_mark_proxy<felica_target>,
                            delete_proxy<felica_target>,
                            selected_felica_target);
}

/*
 * Document-class: Felica::Target
 *
 * Provides access to the Felica card specific requests. Cannot be
 * constructed directly, instead an instance will be returned from
 * +Felica::NFC::Device#select_felica+
 * @see Felica::NFC::Device#select_felica
 */

struct check_nfc_response {
    template<typename T>
    static void check(int res,
                      typename T::request *request,
                      typename T::response *response,
                      size_t requestLen,
                      size_t responseLen)
    {
        if (res < 0) {
            rb_raise(rb_eRuntimeError, "nfc transport error: %d", res);
        }
    }
};

struct check_response_code {
    template<typename T>
    static void check(int res,
                      typename T::request *request,
                      typename T::response *response,
                      size_t requestLen,
                      size_t responseLen)
    {
        if (response->header.code != (T::code + 1)) {
            rb_raise(rb_eRuntimeError, "unexpected response code: 0x%x != 0x%x",
                     response->header.code,
                     T::code + 1);
        }
    }
};

struct check_length {
    template<typename T>
    static void check(int res,
                      typename T::request *request,
                      typename T::response *response,
                      size_t requestLen,
                      size_t responseLen)
    {
        if (res != sizeof(*response)) {
            rb_raise(rb_eRuntimeError, "short read: %d != %lu", res, sizeof(*response));
        }
    }
};

struct check_status_flags {
    template<typename T>
    static void check(int res,
                      typename T::request *request,
                      typename T::response *response,
                      size_t requestLen,
                      size_t responseLen)
    {
        if (response->statusFlag1 != 0 || response->statusFlag2 != 0) {
            rb_funcall(cFelicaStatusError, idRaise, 2,
                       INT2NUM(response->statusFlag1),
                       INT2NUM(response->statusFlag2));
        }
    }
};

template <typename T>
static void run_checks(int res,
                       typename T::request *request,
                       typename T::response *response,
                       size_t requestLen, size_t responseLen){}

template <typename T, typename Check, typename... Checks>
static void run_checks(int res,
                       typename T::request *request,
                       typename T::response *response,
                       size_t requestLen, size_t responseLen)
{
    Check::template check<T>(res, request, response, requestLen, responseLen);
    run_checks<T, Checks...>(res, request, response, requestLen, responseLen);
}

template <typename T, typename... Checks>
static int nfc_felica_checked_transceive(VALUE dev,
                                         typename T::request *request,
                                         typename T::response *response,
                                         size_t requestLen = sizeof(typename T::request),
                                         size_t responseLen = sizeof(typename T::response))
 {
    felica_target *fdev;
    Data_Get_Struct(dev, felica_target, fdev);

    int res = fdev->transceive<T>(request, response, requestLen, responseLen);

    run_checks<T, check_nfc_response, check_response_code, Checks...>(
        res, request, response, requestLen, responseLen);
    return res;
}

/*
 * Get the current mode. Can be used as a simple ping.
 * @return [Fixnum] the current mode
 */
static VALUE nfc_felica_target_get_mode(VALUE self) {
    typedef felica_target::get_mode r;
    r::request req;
    r::response response;
    nfc_felica_checked_transceive<r, check_length>(
        self, &req, &response);
    return INT2NUM(response.mode);
}

/*
 * @overload read_block(service_code, block_index)
 * @return [String] a 16 byte block from the given service at the given index.
 */
static VALUE nfc_felica_target_read_block(VALUE self, VALUE v_service_code, VALUE v_block_index) {
    uint16_t service_code = NUM2UINT(v_service_code);
    uint8_t block_index = NUM2UINT(v_block_index);

    typedef felica_target::read_without_encryption<1 /* service count */,
                                                   1 /* block count */,
                                                   2 /* block list size */> r;
    r::request req;
    req.serviceList[0] = service_code;
    req.blockList[0] = 0x80;
    req.blockList[1] = block_index;

    r::response response;
    nfc_felica_checked_transceive<r, check_status_flags, check_length>(
        self, &req, &response);

    if (response.blockCount != 1) {
        rb_raise(rb_eRuntimeError, "missing block");
    }

    return rb_str_new(reinterpret_cast<const char*>(&response.blockData[0]),
                      sizeof(response.blockData));
}


/*
 * @return [Array] Array of service values.
 */
static VALUE nfc_felica_target_services(VALUE self) {
    typedef felica_target::search_service r;

    r::request request;
    r::response response;

    VALUE arr = rb_ary_new();

    for (int i = 0; ; i++) {
        request.index = i;
        // length is variable, don't check_length
        int res = nfc_felica_checked_transceive<r>(
            self, &request, &response);

        if (res == sizeof(response) - 2) {
            // service code?
            if (response.data[0] == 0xff && response.data[1] == 0xff) {
                break;
            }

            uint16_t c = response.data[0] | (response.data[1] << 8);
            rb_ary_push(arr, UINT2NUM(c));
        }
        else if (res == sizeof(response)) {
            // ???
        }
        else {
            rb_raise(rb_eRuntimeError, "Unexpected response length during service search: %d", res);
            break;
        }
    }
    return arr;
}

extern "C" void Init_felica(void) {
    VALUE cFelica = rb_define_class("Felica", rb_cObject);

    VALUE cNFC = rb_define_class_under(cFelica, "NFC", rb_cObject);
    rb_define_singleton_method(cNFC, "make_context",
                               RUBY_METHOD_FUNC(nfc_make_context), 0);

    cNFCContext = rb_define_class_under(cNFC, "Context", rb_cObject);
    rb_define_private_method(cNFCContext, "open_device_raw",
                             RUBY_METHOD_FUNC(nfc_context_open_device_raw), 0);

    cNFCDevice = rb_define_class_under(cNFC, "Device", rb_cObject);
    rb_define_method(cNFCDevice, "init!",
                     RUBY_METHOD_FUNC(nfc_device_init), 0);
    rb_define_method(cNFCDevice, "close",
                     RUBY_METHOD_FUNC(nfc_device_close), 0);
    rb_define_method(cNFCDevice, "select_felica",
                     RUBY_METHOD_FUNC(nfc_device_select_felica), 0);


    cFelicaTarget = rb_define_class_under(cFelica, "Target", rb_cObject);
    rb_define_method(cFelicaTarget, "get_mode",
                     RUBY_METHOD_FUNC(nfc_felica_target_get_mode), 0);
    rb_define_method(cFelicaTarget, "read_block",
                     RUBY_METHOD_FUNC(nfc_felica_target_read_block), 2);
    rb_define_method(cFelicaTarget, "services",
                     RUBY_METHOD_FUNC(nfc_felica_target_services), 0);

    cFelicaStatusError = rb_const_get(cFelica, rb_intern("FelicaStatusError"));
    idRaise = rb_intern("raise!");
}
