#include <memory>
#include <iostream>
#include <exception>
#include <nfc/nfc.h>

#include "hexdump.h"

struct nfc_exit_deleter {
	void operator()(nfc_context* p) {
		std::cout << "cleaning up nfc" << std::endl;
		if (p) nfc_exit(p);
	}
};
typedef std::unique_ptr<nfc_context, nfc_exit_deleter> context_p;

context_p make_context() {
	nfc_context* p;
	nfc_init(&p);
	return context_p { p };
}

static void nfc_safe_close(nfc_device *pnd) {
	std::cout << "closing device" << std::endl;
	if (pnd) nfc_close(pnd);
}

struct nfc_close_deleter {
	void operator()(nfc_device *pnd) {
		nfc_safe_close(pnd);
	}
};


typedef std::unique_ptr<nfc_device, nfc_close_deleter> device_p;

struct felica_command_header {
	uint8_t len;
	uint8_t commandCode;
	uint8_t idm[8];
} __attribute__((packed));

struct felica_response_header {
	uint8_t len;
	uint8_t commandCode;
	uint8_t idm[8];
} __attribute__((packed));


// the low level device
class felica_device {
	std::shared_ptr<nfc_device> mNFCDevice;
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
		int res = nfc_initiator_transceive_bytes(
			mNFCDevice.get(),
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

	template <typename T, typename Y>
	Y transceiveSimple(const T& command) {
		Y tmp;
		transceiveRaw(reinterpret_cast<const uint8_t*>(&command),
					  sizeof(T),
					  reinterpret_cast<uint8_t*>(&tmp),
					  sizeof(Y));
	}

	
public:
	
	struct get_mode
	{
		constexpr static uint8_t code = 0x04;

		struct request {
			felica_command_header header;
		} __attribute__((packed));

		struct response {
			felica_response_header header;
			uint8_t mode;
		} __attribute__((packed));
	};

	template <int serviceListCount_, int blockListCount_, int blockListSize_>
	struct read_without_encryption {
		constexpr static uint8_t code = 0x06;

		struct request {
			felica_command_header header;
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
			felica_response_header header;
			uint8_t statusFlag1;
			uint8_t statusFlag2;
			uint8_t blockCount;
			uint8_t blockData[16 * blockListCount_];
		} __attribute__((packed));
	};

	
	felica_device(std::shared_ptr<nfc_device> device,
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
	
	// nfc_strerror's argument is an nfc_device. While the return code
	// of transceive is the error (?), we have to dig it out of the
	// device via libnfc's nfc_strerror.
	const nfc_device* nfc_device() {
		return mNFCDevice.get();
	}
};

// the high level representation

class nfc_transport_exception : public std::exception {
	const char * const mWhat;
public:
	nfc_transport_exception(const char* what)
		: mWhat(what)
	{
	}

	nfc_transport_exception(const nfc_device *pnd)
		: mWhat(nfc_strerror(pnd))
	{
	}

	virtual const char* what() const throw()
	{
		return mWhat;
	}
};

static const char * felica_s2_strerror(uint8_t statusCode2) {
	switch(statusCode2) {
		// common
	case 0x00: return "Success";
	case 0x01: return "Purse data under/overflow";
	case 0x02: return "Cashback data exceeded";
	case 0x70: return "Memory error";
	case 0x71: return "Memory warning";

		
		// card-specific
	case 0xA1: return "Illegal Number of Service";
	case 0xA2: return "Illegal command packet (specified Number of Block";
	case 0xA3: return "Illegal Block List (specified order of Service";
	case 0xA4: return "Illegal Service type";
	case 0xA5: return "Access is not allowed";
	case 0xA6: return "Illegal Service Code List";
	case 0xA7: return "Illegal Block List (access mode)";
	case 0xA8: return "Illegal Block Number (access to the specified data is inhibited";
	case 0xA9: return "Data write failure";
	case 0xAA: return "Key-change failure";
	case 0xAB: return "Illegal Package Parity or Illegal Package MAC";
	case 0xAC: return "Illegal parameter";
	case 0xAD: return "Service exists already";
	case 0xAE: return "Illegal System Code";
	case 0xAF: return "Too many simulatenous cyclic write operations";
	case 0xC0: return "Illegal Package Identifier";
	case 0xC1: return "Discrepancy of parameters inside and outside Package";
	case 0xC2: return "Command is disabled already";

	default:
		return nullptr;
	}
}

static const char * felica_s1_strerror(uint8_t statusCode1) {
	switch (statusCode1) {
	case 0x00: return "Success";
	case 0xFF: return "Error (no block list)";
	default:
		return "Error (block list)";
	}
}

static const char *felica_strerror(uint8_t statusCode1, uint8_t statusCode2) {
	const char* result;
	if (statusCode2 != 0) {
		result = felica_s2_strerror(statusCode2);;
		if (result)
			return result;
	}
	return felica_s1_strerror(statusCode1);
}

class felica_exception : public std::exception {
	const char * const mWhat;
public:
	felica_exception(const char* what)
		: mWhat(what)
	{
	}

	felica_exception(uint8_t statusCode1, uint8_t statusCode2)
		: mWhat(felica_strerror(statusCode1, statusCode2))
	{
	}

	virtual const char* what() const throw()
	{
		return mWhat;
	}
};

class felica_data {
	std::shared_ptr<felica_device> mDevice;

	template<typename T>
	int checkedTransceive(typename T::request *req,
						   typename T::response *response) {
		int res = mDevice->transceive<T>(req, response);
		if (res < 0) {
			throw nfc_transport_exception(mDevice->nfc_device());
		}
		return res;
	}

	template<typename T>
	int lenCheckedTransceive(typename T::request *req,
							  typename T::response *response) {
		int res = checkedTransceive<T>(req, response);
		if (res != sizeof(*response)) {
			throw felica_exception("short read");
		}
		return res;
	}

public:
 	felica_data(std::shared_ptr<felica_device> device)
		: mDevice(device)
	{
	}

	uint8_t get_mode() {
		typedef felica_device::get_mode r;
		r::request req;
		r::response response;
		lenCheckedTransceive<r>(&req, &response);
		return response.mode;
	}

	void read_block(uint16_t service_code, uint8_t block_index, uint8_t blockData[16]) {
		typedef felica_device::read_without_encryption<1, 1, 2> r;
		r::request req;
		req.serviceList[0] = service_code;
		req.blockList[0] = 0x80;
		req.blockList[1] = block_index;
		
		r::response response;
		int res = checkedTransceive<r>(&req, &response);

		if (response.statusFlag1 != 0 || response.statusFlag2 != 0) {
			throw felica_exception(response.statusFlag1, response.statusFlag2);
		}

		if (res != sizeof(response)) {
			throw felica_exception("short read");
		}

		memcpy(&blockData[0], &response.blockData[0], 16);

		return;
	}
};

struct transaction_details {
	uint8_t terminalSpecies;
	uint8_t processing;
	uint8_t padding[2];
	uint16_t date;
	uint8_t onLineSection;
	uint8_t iriekiOrder;
	uint8_t outTheTrainSchedule;
	uint8_t outStationOrder;
	uint16_t balance;
	uint8_t serialNumber[3];
	uint8_t region;
} __attribute__((packed));

struct suica_date {
	int year;
	int month;
	int day;
	
	void set_date(uint16_t date) {
		year = (date >> 9);
		month = (date >> 5) & 0xf;
		day = date & 0x1f;
	}

	suica_date(uint16_t date) {
		set_date(date);
	}

	friend std::ostream& operator<<(std::ostream& os, const suica_date& date) {
		return os << (date.year + 2000) << "-" << date.month << "-" << date.day;
	}
};

static void print_transaction(const transaction_details& details) {
	std::cout << (int) details.terminalSpecies << ","
			  << (int) details.processing << ","
			  << suica_date{details.date} << ","
			  << (int) details.iriekiOrder << ","
			  << (int) details.outTheTrainSchedule << ","
			  << (int) details.outStationOrder << ","
			  << details.balance << ","
			  << details.serialNumber << ","
			  << (int) details.region
			  << std::endl;
		
}

void ping() {
	std::cout << "Using libnfc " << nfc_version() << std::endl;

	context_p context = make_context();
	if (!context) {
		// TODO except?
		std::cerr << "Make context failed" << std::endl;
		return;
	}

	std::shared_ptr<nfc_device> device {
		nfc_open(context.get(), NULL), nfc_safe_close};

	if (!device) {
		std::cerr << "Open device failed" << std::endl;
		return;
	}

	if (nfc_initiator_init(device.get()) < 0) {
		nfc_perror(device.get(), "nfc_initiator_init");
		return;
	}

	nfc_modulation modulation;
	modulation.nmt = NMT_FELICA;
	modulation.nbr = NBR_212;

	nfc_target target;

	int found_targets = 0;

	std::cout << "Waiting for token..." << std::endl;

	found_targets = nfc_initiator_select_passive_target(
		device.get(), modulation, NULL, 0, &target);

	if (found_targets < 0) {
		nfc_perror(device.get(), "nfc_initiator_select_passive_target");
		return;
	}

	std::cout << "Found token with id: " << std::endl
			  << hexdump(target.nti.nfi.abtId) << std::endl
			  << "abdPad:" << std::endl << hexdump(target.nti.nfi.abtPad) << std::endl;

	std::shared_ptr<felica_device> dev { new felica_device(device, target) };
	felica_data d { dev };
	uint8_t mode = d.get_mode();
	std::cout << "mode: " << (int) mode << std::endl;

	for (uint8_t i = 0; i < 32; i++) {
		struct transaction_details  block;
		static_assert(sizeof(block) == 16, "block size is 16 bytes");
		d.read_block(0x090f, i, reinterpret_cast<uint8_t*>(&block));

		std::cout << "block " << (int) i << " contents:" << std::endl
				  << hexdump(&block) << std::endl;

		if (block.terminalSpecies == 0) {
			return;
		}
		
		block.date = ntohs(block.date);
		// block.balance = ntohs(block.balance);

		print_transaction(block);
	}
}

int main(int argc, char** argv) {
	(void) argc;
	(void) argv;

	ping();

	return 0;
}
