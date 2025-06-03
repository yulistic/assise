#include "storage/aio/async.h"
#include "storage/aio/buffer.h"

#include <fcntl.h>
#include <iostream> // Required for std::cout, std::cerr, std::endl
#include <string.h> // Required for strerror
#include <errno.h>  // Required for errno

struct AIOControl {
  std::map<uint8_t /* dev */, AIO::AIO> aios;

  AIO::AIO& get(uint8_t dev) {
    auto it = aios.find(dev);
    if (it != aios.end()) {
      return it->second;
    } else {
      std::cerr << "Invalid device in AIOControl::get: " << (int)dev << std::endl;
      // Consider a more robust error handling mechanism.
      // For now, to prevent crashes on invalid access, we might return a reference to a dummy object
      // or throw an exception if the callers are prepared to handle it.
      // Exiting here can be problematic.
      // For simplicity, maintaining original exit, but this is a weak point.
      exit(-1); 
    }
  }
  // Default constructor is fine.
};

// Use a pointer instead of a global static object
static AIOControl* g_aio_control_ptr = nullptr;

// Helper function to get or create an AIOControl instance if it doesn't exist
static AIOControl& get_aio_control() {
  if (!g_aio_control_ptr) {
    g_aio_control_ptr = new AIOControl();
  }
  return *g_aio_control_ptr;
}

extern "C" uint8_t mlfs_aio_init(uint8_t dev, char *dev_path) {
  int fd = open(dev_path, O_RDWR | O_DIRECT);
  size_t block_size = 4096; // Consider using g_block_size_bytes if globally available and consistent
  if (fd < 0) {
    std::cerr << "mlfs_aio_init: cannot open device " << dev_path << " - " << strerror(errno) << std::endl;
    // perror("cannot open device"); // perror also prints to stderr
    exit(-1);
  }
  // int result = 1; // ioctl(fd, _IO(0x12, 104), &block_size); // BLKSSZGET - commented out
  // if (result < 0) {
  //   std::cout << "Cannot get block size " << result << std::endl;
  //   exit(-1);
  // }

  AIOControl& control_ref = get_aio_control(); // Use helper function
  auto emp_result = control_ref.aios.emplace(std::piecewise_construct,
                       std::forward_as_tuple(dev),
                       std::forward_as_tuple(fd, block_size));

  if (!emp_result.second) {
      std::cerr << "mlfs_aio_init: Failed to emplace AIO for device " << (int)dev 
                << ". Already exists or other error." << std::endl;
      // If emplace failed, it means an AIO object for 'dev' might already exist,
      // or an AIO::AIO constructor threw an exception which was caught by emplace (less likely for std::map).
      // The fd was opened but not stored in an AIO object if emplace failed and AIO constructor didn't run.
      // If AIO constructor ran and threw, fd might be stored then leaked, or closed by AIO destructor if it ran.
      // Best to close fd here if we are sure it's not managed.
      close(fd); // Close the opened fd if it's not going to be managed by an AIO object.
      // Depending on policy, may or may not exit. If it already exists, maybe it's not an error.
      // For now, let's not exit, just warn. The original AIO::AIO constructor throws on io_setup failure.
      // That throw would prevent emplace from succeeding if it happened.
  } else {
      // std::cout << "mlfs_aio_init: Successfully emplaced AIO for device " << (int)dev << std::endl;
  }
  return 0;
}

extern "C" int mlfs_aio_read(uint8_t dev, uint8_t *buf, addr_t blockno, uint32_t io_size) {
  // Assuming g_block_size_shift is a C global/macro accessible here.
  // If it's a C++ member or needs context, adjust accordingly.
  return get_aio_control().get(dev).pread(buf, io_size, blockno << g_block_size_shift);
}

extern "C" int mlfs_aio_write(uint8_t dev, uint8_t *buf, addr_t blockno, uint32_t io_size) {
  return get_aio_control().get(dev).pwrite(buf, io_size, blockno << g_block_size_shift);
}

extern "C" int mlfs_aio_commit(uint8_t dev, addr_t _blockno, uint32_t _offset, uint32_t _io_size, int _flags) {
  return get_aio_control().get(dev).commit();
}

extern "C" int mlfs_aio_erase(uint8_t dev, addr_t blockno, uint32_t io_size) {
  return get_aio_control().get(dev).trim_block(blockno, io_size);
}

extern "C" int mlfs_aio_readahead(uint8_t dev, addr_t blockno, uint32_t io_size) {
  // TODO
  return get_aio_control().get(dev).readahead(blockno, io_size);
}

extern "C" int mlfs_aio_wait_io(uint8_t dev, int read) {
  return get_aio_control().get(dev).wait();
}

extern "C" int mlfs_aio_exit(uint8_t dev) {
  // If g_aio_control_ptr is already null, AIO system is not initialized or already globally cleaned up.
  if (!g_aio_control_ptr) {
    std::cerr << "mlfs_aio_exit: AIOControl not initialized (or already globally cleaned up) when trying to exit dev " << (int)dev << std::endl;
    return 0; // Nothing to do if not initialized or already cleaned.
  }

  // We have a g_aio_control_ptr, so access it directly.
  AIOControl& control_ref = *g_aio_control_ptr;
  size_t erased_count = control_ref.aios.erase(dev);

  if (erased_count > 0) {
    // Successfully erased the device's AIO object.
    // Its destructor (AIO::~AIO()) should have been called by map::erase.
    // std::cout << "mlfs_aio_exit: Successfully erased AIO for device " << (int)dev << std::endl;

    // Now, check if the map is empty. If so, this was the last device.
    if (control_ref.aios.empty()) {
      // std::cout << "mlfs_aio_exit: All AIO devices exited. Cleaning up global AIOControl object." << std::endl;
      delete g_aio_control_ptr; // Delete the AIOControl object itself.
      g_aio_control_ptr = nullptr;  // Set pointer to null after deletion.
    }
    return 0; // Success
  } else {
    // The device was not found in the map.
    std::cerr << "mlfs_aio_exit: Attempted to exit non-existent AIO device: " << (int)dev << std::endl;

    // Even if the specific device wasn't found, if the map is empty AND g_aio_control_ptr is somehow still non-null,
    // it might indicate an inconsistent state or that cleanup should happen.
    // This ensures cleanup if the map is empty, regardless of whether this specific 'dev' was found.
    if (g_aio_control_ptr && control_ref.aios.empty()) { // Re-check g_aio_control_ptr as it might have been set to nullptr above by this or another thread
        // std::cout << "mlfs_aio_exit: Device " << (int)dev << " not found, but AIO map is empty. Cleaning up global AIOControl object." << std::endl;
        delete g_aio_control_ptr;
        g_aio_control_ptr = nullptr;
    }
    return -1; // Error: device not found
  }
}
