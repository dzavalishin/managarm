
namespace thor {

void *physicalToVirtual(uintptr_t address);

template<typename T>
T *accessPhysical(PhysicalAddr address) {
	return (T *)physicalToVirtual(address);
}

template<typename T>
T *accessPhysicalN(PhysicalAddr address, int n) {
	return (T *)physicalToVirtual(address);
}

enum {
	kPageSize = 0x1000
};

class PageSpace {
public:
	enum : uint32_t {
		kAccessWrite = 1,
		kAccessExecute = 2
	};

	PageSpace(uintptr_t pml4_address);
	
	void switchTo();

	PageSpace clone();

	void mapSingle4k(VirtualAddr pointer, PhysicalAddr physical,
			bool user_access, uint32_t flags);
	PhysicalAddr unmapSingle4k(VirtualAddr pointer);

private:
	uintptr_t p_pml4Address;
};

extern LazyInitializer<PageSpace> kernelSpace;

} // namespace thor
