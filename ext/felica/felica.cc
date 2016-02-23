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

// the low level device
class felica_device {
    struct command_header {
        uint8_t len;
        uint8_t commandCode;
        uint8_t idm[8];
    } __attribute__((packed));

    struct response_header {
        uint8_t len;
        uint8_t commandCode;
        uint8_t idm[8];
    } __attribute__((packed));


    VALUE mNFCDevice;
    nfc_target mTarget;

    template <typename T>
    void fill_header(T* command, uint8_t commandCode) {
        command->header.len = sizeof(T);
        command->header.commandCode = commandCode;
        memcpy(&command->header.idm,
               mTarget.nti.nfi.abtId,
               sizeof(command->header.idm));
    }

    int transceiveRaw(const uint8_t *send, uint8_t sendLen,
                      uint8_t *recv, uint8_t recvLen,
                      int timeout = 300)
    {
        nfc_device *dev;
        Data_Get_Struct(mNFCDevice, nfc_device, dev);
        
        int res = nfc_initiator_transceive_bytes(
            dev,
            send, sendLen,
            recv, recvLen,
            timeout);
        return res;
    }

    template <typename T, typename Y>
    int transceiveCommand(uint8_t commandCode, T* command, Y* response) {
        fill_header(command, commandCode);
        return transceiveRaw(reinterpret_cast<const uint8_t*>(command),
                             sizeof(T),
                             reinterpret_cast<uint8_t*>(response),
                             sizeof(Y));
    }

public:
    void rb_mark() {
        ::rb_gc_mark(mNFCDevice);
    }
    
    struct get_mode
    {
        constexpr static uint8_t code = 0x04;

        struct request {
            felica_device::command_header header;
        } __attribute__((packed));

        struct response {
            felica_device::response_header header;
            uint8_t mode;
        } __attribute__((packed));
    };

    template <int serviceListCount_, int blockListCount_, int blockListSize_>
    struct read_without_encryption {
        constexpr static uint8_t code = 0x06;

        struct request {
            felica_device::command_header header;
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
            felica_device::response_header header;
            uint8_t statusFlag1;
            uint8_t statusFlag2;
            uint8_t blockCount;
            uint8_t blockData[16 * blockListCount_];
        } __attribute__((packed));
    };


    felica_device(VALUE device,
                  const nfc_target& target)
        : mNFCDevice(device)
        , mTarget(target)
    {
    }

    template <typename T>
    int transceive(typename T::request* request,
                   typename T::response* response) {
        return transceiveCommand(T::code, request, response);
    }
};

static VALUE cNFCContext;
static VALUE cNFCDevice;
static VALUE cFelicaDevice;

static VALUE cFelicaStatusError;
static ID idRaise;

static VALUE nfc_make_context(VALUE klass) {
    nfc_context* p = nullptr;
    nfc_init(&p);

    if (!p)
        rb_raise(rb_eRuntimeError, "Failed to open nfc context");

    return Data_Wrap_Struct(cNFCContext, nullptr, nfc_exit, p);
}

static VALUE nfc_context_open_device(VALUE self) {
    nfc_context *ctx;
    Data_Get_Struct(self, nfc_context, ctx);
    
    nfc_device *dev = nfc_open(ctx, NULL);
    if (!dev)
        rb_raise(rb_eRuntimeError, "Failed to open nfc device");

    return Data_Wrap_Struct(cNFCDevice, nullptr, nfc_close, dev);
}

static VALUE nfc_device_init(VALUE self) {
    nfc_device *dev;
    Data_Get_Struct(self, nfc_device, dev);

    if (nfc_initiator_init(dev) < 0) {
        rb_raise(rb_eRuntimeError, "Failed to set initiator");
    }

    return Qnil;
}

static VALUE nfc_device_select_felica(VALUE self) {
    nfc_device *dev;
    Data_Get_Struct(self, nfc_device, dev);

    nfc_modulation modulation;
    modulation.nmt = NMT_FELICA;
    modulation.nbr = NBR_212;

    nfc_target target;
    
    int found_targets = nfc_initiator_select_passive_target(
        dev, modulation, NULL, 0, &target);

    if (found_targets < 0) {
        rb_raise(rb_eRuntimeError, "Failed to select passive target");
    }

    if (found_targets == 0) {
        rb_raise(rb_eRuntimeError, "Missing target");
    }

    felica_device *felica_dev = new felica_device(self, target);

    return Data_Wrap_Struct(cFelicaDevice,
                            rb_mark_proxy<felica_device>,
                            delete_proxy<felica_device>,
                            felica_dev);
}

struct check_length {
    template<typename T>
    void check(int res,
               typename T::request *request,
               typename T::response *response)
    {
        if (res != sizeof(*response)) {
            rb_raise(rb_eRuntimeError, "short read: %d != %lu", res, sizeof(*response));
        }
    }
};

struct check_status_flags {
    template<typename T>
    void check(int res,
               typename T::request *request,
               typename T::response *response)
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
                       typename T::response *response){}

template <typename T, typename Check, typename... Checks>
static void run_checks(int res,
                  typename T::request *request,
                  typename T::response *response)
{
    Check c;
    c.template check<T>(res, request, response);
    run_checks<T, Checks...>(res, request, response);
}

template <typename T, typename... Checks>
static int nfc_felica_checked_transceive(VALUE dev,
                                         typename T::request *request,
                                         typename T::response *response)
{
    felica_device *fdev;
    Data_Get_Struct(dev, felica_device, fdev);

    int res = fdev->transceive<T>(request, response);

    if (res < 0) {
        rb_raise(rb_eRuntimeError, "nfc transport error: %d", res);
    }

    run_checks<T, Checks...>(res, request, response);
    return res;
}

static VALUE nfc_felica_device_get_mode(VALUE self) {
    typedef felica_device::get_mode r;
    r::request req;
    r::response response;
    nfc_felica_checked_transceive<r, check_length>(
        self, &req, &response);
    return INT2NUM(response.mode);
}

static VALUE nfc_felica_device_read_block(VALUE self, VALUE v_service_code, VALUE v_block_index) {
    uint16_t service_code = NUM2UINT(v_service_code);
    uint8_t block_index = NUM2UINT(v_block_index);

    typedef felica_device::read_without_encryption<1 /* service count */,
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


extern "C" void Init_felica(void) {
    typedef VALUE(*ruby_method)(ANYARGS);

    VALUE mFelica = rb_define_module("Felica");
    
    VALUE cNFC = rb_define_class_under(mFelica, "NFC", rb_cObject);
    rb_define_singleton_method(cNFC, "make_context",
                               reinterpret_cast<ruby_method>(nfc_make_context), 0);

    cNFCContext = rb_define_class_under(cNFC, "Context", rb_cObject);
    rb_define_method(cNFCContext, "open_device",
                     reinterpret_cast<ruby_method>(nfc_context_open_device), 0);

    cNFCDevice = rb_define_class_under(cNFC, "Device", rb_cObject);
    rb_define_method(cNFCDevice, "init!",
                     reinterpret_cast<ruby_method>(nfc_device_init), 0);
    rb_define_method(cNFCDevice, "select_felica",
                     reinterpret_cast<ruby_method>(nfc_device_select_felica), 0);

    cFelicaDevice = rb_define_class_under(mFelica, "Device", rb_cObject);
    rb_define_method(cFelicaDevice, "get_mode",
                     reinterpret_cast<ruby_method>(nfc_felica_device_get_mode), 0);
    rb_define_method(cFelicaDevice, "read_block",
                     reinterpret_cast<ruby_method>(nfc_felica_device_read_block), 2);

    cFelicaStatusError = rb_const_get(mFelica, rb_intern("FelicaStatusError"));
    idRaise = rb_intern("raise!");
}