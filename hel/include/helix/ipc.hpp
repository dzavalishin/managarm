
#ifndef HELIX_HPP
#define HELIX_HPP

#include <atomic>
#include <initializer_list>
#include <stdexcept>

#include <hel.h>
#include <hel-syscalls.h>

namespace helix {

struct UniqueDescriptor {
	friend void swap(UniqueDescriptor &a, UniqueDescriptor &b) {
		using std::swap;
		swap(a._handle, b._handle);
	}

	UniqueDescriptor()
	: _handle(kHelNullHandle) { }
	
	UniqueDescriptor(const UniqueDescriptor &other) = delete;

	UniqueDescriptor(UniqueDescriptor &&other)
	: UniqueDescriptor() {
		swap(*this, other);
	}

	explicit UniqueDescriptor(HelHandle handle)
	: _handle(handle) { }

	~UniqueDescriptor() {
		if(_handle != kHelNullHandle)
			HEL_CHECK(helCloseDescriptor(_handle));
	}

	UniqueDescriptor &operator= (UniqueDescriptor other) {
		swap(*this, other);
		return *this;
	}

	HelHandle getHandle() const {
		return _handle;
	}

	void release() {
		_handle = kHelNullHandle;
	}

private:
	HelHandle _handle;
};

struct BorrowedDescriptor {
	BorrowedDescriptor()
	: _handle(kHelNullHandle) { }
	
	BorrowedDescriptor(const BorrowedDescriptor &other) = default;
	BorrowedDescriptor(BorrowedDescriptor &&other) = default;

	explicit BorrowedDescriptor(HelHandle handle)
	: _handle(handle) { }
	
	BorrowedDescriptor(const UniqueDescriptor &other)
	: BorrowedDescriptor(other.getHandle()) { }

	~BorrowedDescriptor() = default;

	BorrowedDescriptor &operator= (const BorrowedDescriptor &) = default;

	HelHandle getHandle() const {
		return _handle;
	}

private:
	HelHandle _handle;
};

template<typename Tag>
struct UniqueResource : UniqueDescriptor {
	UniqueResource() = default;

	explicit UniqueResource(HelHandle handle)
	: UniqueDescriptor(handle) { }

	UniqueResource(UniqueDescriptor descriptor)
	: UniqueDescriptor(std::move(descriptor)) { }
};

template<typename Tag>
struct BorrowedResource : BorrowedDescriptor {
	BorrowedResource() = default;

	explicit BorrowedResource(HelHandle handle)
	: BorrowedDescriptor(handle) { }

	BorrowedResource(BorrowedDescriptor descriptor)
	: BorrowedDescriptor(descriptor) { }

	BorrowedResource(const UniqueResource<Tag> &other)
	: BorrowedDescriptor(other) { }

	UniqueResource<Tag> dup() {
		HelHandle new_handle;
		HEL_CHECK(helTransferDescriptor(getHandle(), kHelThisUniverse, &new_handle));
		return UniqueResource<Tag>(new_handle);
	}
};

struct Hub { };
using UniqueHub = UniqueResource<Hub>;
using BorrowedHub = BorrowedResource<Hub>;

inline UniqueHub createHub() {
	HelHandle handle;
	HEL_CHECK(helCreateEventHub(&handle));
	return UniqueHub(handle);
}

struct Pipe { };
using UniquePipe = UniqueResource<Pipe>;
using BorrowedPipe = BorrowedResource<Pipe>;

inline std::pair<UniquePipe, UniquePipe> createFullPipe() {
	HelHandle first_handle, second_handle;
	HEL_CHECK(helCreateFullPipe(&first_handle, &second_handle));
	return { UniquePipe(first_handle), UniquePipe(second_handle) };
}

struct Lane { };
using UniqueLane = UniqueResource<Lane>;
using BorrowedLane = BorrowedResource<Lane>;

inline std::pair<UniqueLane, UniqueLane> createStream() {
	HelHandle first_handle, second_handle;
	HEL_CHECK(helCreateStream(&first_handle, &second_handle));
	return { UniqueLane(first_handle), UniqueLane(second_handle) };
}

struct Irq { };
using UniqueIrq = UniqueResource<Irq>;
using BorrowedIrq = BorrowedResource<Irq>;

// we use a pattern similar to CRTP here to reduce code size.
template<typename Result>
struct OperationBase : Result {
	friend class Dispatcher;

	OperationBase()
	: _asyncId(0) { }
	
	virtual ~OperationBase() { }

protected:
	virtual void complete() = 0;

	int64_t _asyncId;
};

template<typename Result, typename M>
struct Operation : OperationBase<Result> {
	typename M::Future future() {
		return _completer.future();
	}

protected:
	void complete() override final {
		_completer();
	}

private:
	typename M::Completer _completer;
};

struct ResultBase {
	// TODO: replace by std::error_code
	HelError error() {
		return _error;
	}

protected:
	HelError _error;
};

struct RecvStringResult : ResultBase {
	size_t actualLength() {
		HEL_CHECK(_error);
		return _actualLength;
	}

	int64_t requestId() {
		HEL_CHECK(_error);
		return _requestId;
	}
	int64_t sequenceId() {
		HEL_CHECK(_error);
		return _sequenceId;
	}

protected:
	size_t _actualLength;
	int64_t _requestId;
	int64_t _sequenceId;
};

struct RecvDescriptorResult : ResultBase {
	UniqueDescriptor descriptor() {
		UniqueDescriptor descriptor(_handle);
		_handle = kHelNullHandle;
		return descriptor;
	}

	int64_t requestId() {
		HEL_CHECK(_error);
		return _requestId;
	}
	int64_t sequenceId() {
		HEL_CHECK(_error);
		return _sequenceId;
	}

protected:
	HelHandle _handle;
	int64_t _requestId;
	int64_t _sequenceId;
};

struct SendStringResult : ResultBase {
};

struct SendDescriptorResult : ResultBase {
};

struct AwaitIrqResult : ResultBase {
};

struct Dispatcher {
	using RecvString = OperationBase<RecvStringResult>;
	using RecvDescriptor = OperationBase<RecvDescriptorResult>;
	using SendString = OperationBase<SendStringResult>;
	using SendDescriptor = OperationBase<SendDescriptorResult>;
	using AwaitIrq = OperationBase<AwaitIrqResult>;
	
	static Dispatcher &global();

	explicit Dispatcher(UniqueHub hub)
	: _hub(std::move(hub)) { }

	BorrowedHub getHub() const {
		return _hub;
	}

	void dispatch() {
		static constexpr int kEventsPerCall = 16;

		HelEvent list[kEventsPerCall];
		size_t num_items;
		HEL_CHECK(helWaitForEvents(_hub.getHandle(), list, kEventsPerCall,
				kHelWaitInfinite, &num_items));

		for(size_t i = 0; i < num_items; i++) {
			HelEvent &e = list[i];
			switch(e.type) {
			case kHelEventRecvString: {
				auto ptr = static_cast<RecvString *>((void *)e.submitObject);
				ptr->_error = e.error;
				ptr->_actualLength = e.length;
				ptr->_requestId = e.msgRequest;
				ptr->_sequenceId = e.msgSequence;
				ptr->complete();
			} break;
			case kHelEventRecvDescriptor: {
				auto ptr = static_cast<RecvDescriptor *>((void *)e.submitObject);
				ptr->_error = e.error;
				ptr->_handle = e.handle;
				ptr->_requestId = e.msgRequest;
				ptr->_sequenceId = e.msgSequence;
				ptr->complete();
			} break;
			case kHelEventSendString: {
				auto ptr = static_cast<SendString *>((void *)e.submitObject);
				ptr->_error = e.error;
				ptr->complete();
			} break;
			case kHelEventSendDescriptor: {
				auto ptr = static_cast<SendDescriptor *>((void *)e.submitObject);
				ptr->_error = e.error;
				ptr->complete();
			} break;
			case kHelEventIrq: {
				auto ptr = static_cast<AwaitIrq *>((void *)e.submitObject);
				ptr->_error = e.error;
				ptr->complete();
			} break;
			default:
				throw std::runtime_error("Unknown event type");
			}
		}
	}

private:
	UniqueHub _hub;
};

template<typename M>
struct RecvString : Operation<RecvStringResult, M> {
	RecvString() = default;

	RecvString(Dispatcher &dispatcher, BorrowedPipe pipe, void *buffer, size_t max_length,
			int64_t msg_request, int64_t msg_seq, uint32_t flags) {
		auto error = helSubmitRecvString(pipe.getHandle(), dispatcher.getHub().getHandle(),
				(uint8_t *)buffer, max_length, msg_request, msg_seq,
				0, (uintptr_t)this, flags, &this->_asyncId);
		if(error) {
			this->_error = error;
			this->complete();
		}
	}
};

template<typename M>
struct RecvDescriptor : Operation<RecvDescriptorResult, M> {
	RecvDescriptor(Dispatcher &dispatcher, BorrowedPipe pipe,
			int64_t msg_request, int64_t msg_seq, uint32_t flags) {
		auto error = helSubmitRecvDescriptor(pipe.getHandle(), dispatcher.getHub().getHandle(),
				msg_request, msg_seq, 0, (uintptr_t)this, flags, &this->_asyncId);
		if(error) {
			this->_error = error;
			this->complete();
		}
	}
};

template<typename M>
struct SendString : Operation<SendStringResult, M> {
	SendString() = default;

	SendString(Dispatcher &dispatcher, BorrowedPipe pipe, const void *buffer, size_t length,
			int64_t msg_request, int64_t msg_seq, uint32_t flags) {
		auto error = helSubmitSendString(pipe.getHandle(), dispatcher.getHub().getHandle(),
				(const uint8_t *)buffer, length, msg_request, msg_seq,
				0, (uintptr_t)this, flags, &this->_asyncId);
		if(error) {
			this->_error = error;
			this->complete();
		}
	}
};

template<typename M>
struct SendDescriptor : Operation<SendDescriptorResult, M> {
	SendDescriptor(Dispatcher &dispatcher, BorrowedPipe pipe, BorrowedDescriptor descriptor,
			int64_t msg_request, int64_t msg_seq, uint32_t flags) {
		auto error = helSubmitSendDescriptor(pipe.getHandle(), dispatcher.getHub().getHandle(),
				descriptor.getHandle(), msg_request, msg_seq,
				0, (uintptr_t)this, flags, &this->_asyncId);
		if(error) {
			this->_error = error;
			this->complete();
		}
	}
};

template<typename M>
struct AwaitIrq : Operation<AwaitIrqResult, M> {
	AwaitIrq(Dispatcher &dispatcher, BorrowedIrq irq) {
		auto error = helSubmitWaitForIrq(irq.getHandle(), dispatcher.getHub().getHandle(),
				0, (uintptr_t)this, &this->_asyncId);
		if(error) {
			this->_error = error;
			this->complete();
		}
	}
};

// ----------------------------------------------------------------------------
// Experimental: submitAsync
// ----------------------------------------------------------------------------

template<typename M>
HelAction action(SendString<M> *operation, const void *buffer, size_t length,
		uint32_t flags = 0) {
	HelAction action;
	action.type = kHelActionSendFromBuffer;
	action.context = (uintptr_t)operation;
	action.flags = flags;
	action.buffer = const_cast<void *>(buffer);
	action.length = length;
	return action;
}

template<typename M>
HelAction action(RecvString<M> *operation, void *buffer, size_t length,
		uint32_t flags = 0) {
	HelAction action;
	action.type = kHelActionRecvToBuffer;
	action.context = (uintptr_t)operation;
	action.flags = flags;
	action.buffer = buffer;
	action.length = length;
	return action;
}

template<size_t N>
void submitAsync(BorrowedDescriptor descriptor, HelAction (&&actions)[N],
		const Dispatcher &dispatcher) {
	HEL_CHECK(helSubmitAsync(descriptor.getHandle(), actions, N,
			dispatcher.getHub().getHandle(), 0));
}

} // namespace helix

#endif // HELIX_HPP
