
#ifndef POSIX_SUBSYSTEM_DEV_FS_HPP
#define POSIX_SUBSYSTEM_DEV_FS_HPP

#include <frigg/string.hpp>
#include <frigg/hashmap.hpp>

#include "vfs.hpp"

namespace dev_fs {

// --------------------------------------------------------
// Inode
// --------------------------------------------------------

struct Inode {
	virtual StdSharedPtr<VfsOpenFile> openSelf(StdUnsafePtr<Process> process) = 0;
};

// --------------------------------------------------------
// CharDeviceNode
// --------------------------------------------------------

class CharDeviceNode : public Inode {
public:
	class OpenFile : public VfsOpenFile {
	public:
		OpenFile(StdSharedPtr<Device> device);
		
		// inherited from VfsOpenFile
		void write(const void *buffer, size_t length) override;
		void read(void *buffer, size_t max_length, size_t &actual_length) override;
	
	private:
		StdSharedPtr<Device> p_device;
	};

	CharDeviceNode(unsigned int major, unsigned int minor);

	// inherited from Inode
	StdSharedPtr<VfsOpenFile> openSelf(StdUnsafePtr<Process> process) override;

private:
	unsigned int major, minor;
};

// --------------------------------------------------------
// HelfdNode
// --------------------------------------------------------

class HelfdNode : public Inode {
public:
	class OpenFile : public VfsOpenFile {
	public:
		OpenFile(HelfdNode *inode);
		
		// inherited from VfsOpenFile
		void setHelfd(HelHandle handle) override;
		HelHandle getHelfd() override;
	
	private:
		HelfdNode *p_inode;
	};

	// inherited from Inode
	StdSharedPtr<VfsOpenFile> openSelf(StdUnsafePtr<Process> process) override;

private:
	HelHandle p_handle;
};

// --------------------------------------------------------
// DirectoryNode
// --------------------------------------------------------

struct DirectoryNode : public Inode {
	DirectoryNode();

	StdSharedPtr<VfsOpenFile> openRelative(StdUnsafePtr<Process> process,
			frigg::StringView path, uint32_t flags, uint32_t mode);
	
	// inherited from Inode
	StdSharedPtr<VfsOpenFile> openSelf(StdUnsafePtr<Process> process) override;

	frigg::Hashmap<frigg::String<Allocator>, StdSharedPtr<Inode>,
			frigg::DefaultHasher<frigg::StringView>, Allocator> entries;
};

// --------------------------------------------------------
// MountPoint
// --------------------------------------------------------

class MountPoint : public VfsMountPoint {
public:
	MountPoint();
	
	DirectoryNode *getRootDirectory() {
		return &rootDirectory;
	}
	
	// inherited from VfsMountPoint
	StdSharedPtr<VfsOpenFile> openMounted(StdUnsafePtr<Process> process,
			frigg::StringView path, uint32_t flags, uint32_t mode) override;
	
private:
	DirectoryNode rootDirectory;
};

} // namespace dev_fs

#endif // POSIX_SUBSYSTEM_DEV_FS_HPP
