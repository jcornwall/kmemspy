#include "../kernel/kmemspy.h"

#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <stdint.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <vector>

// Test a condition and throw a formatted string exception if it fails.
#define mAssert(COND, FORMAT_STR, ...)                                         \
  if ((COND) == false) {                                                       \
    int message_len = std::snprintf(nullptr, 0, FORMAT_STR, ## __VA_ARGS__);   \
    std::vector<char> message(message_len + 1);                                \
    std::snprintf(message.data(), message.size(), FORMAT_STR, ## __VA_ARGS__); \
    throw std::runtime_error(std::string(message.begin(), message.end()));     \
  }

class Application {
public:
  Application(int argc,
              char *argv[])
    : pid_(0)
    , addr_is_phys_(false)
    , region_start_(0)
    , region_size_(0)
  {
    // Parse command-line arguments.
    const char *usage_msg = "usage: kmemspy [--phys | -p pid] <addr_hex> <size_hex>";
    bool have_pid = false;

    struct option long_opts[] = {
      { "phys", no_argument, nullptr, 0 },
      { nullptr, 0, nullptr, 0 }
    };

    for (int opt; (opt = getopt_long(argc, argv, "p:", long_opts, nullptr)) != -1; ) {
      if (opt == 0) {
        addr_is_phys_ = true;
      }
      else if (opt == 'p') {
        std::stringstream(optarg) >> pid_;
        have_pid = true;
      }
      else {
        mAssert(false, "%s", usage_msg);
      }
    }

    mAssert(optind == (argc - 2) && (addr_is_phys_ != have_pid), "%s", usage_msg);

    std::stringstream(argv[optind + 0]) >> std::hex >> region_start_;
    std::stringstream(argv[optind + 1]) >> std::hex >> region_size_;

    mAssert((region_start_ & 0x3) == 0, "address must be 4-byte aligned");
    mAssert((region_size_ & 0x3) == 0, "size must be 4-byte aligned");

    // Open the kernel device.
    dev_fd_ = open("/dev/kmemspy", O_RDWR);
    mAssert(dev_fd_ >= 0, "failed to open /dev/kmemspy");

    // Retrieve the system page size.
    page_size_ = getpagesize();
  }

  void run() {
    // Read memory contents into a buffer and record PTEs.
    uint64_t region_end = region_start_ + region_size_;
    uint64_t pfn_start = region_start_ / page_size_;
    uint64_t pfn_end = (region_end + (page_size_ - 1)) / page_size_;

    std::vector<uint8_t> region_data;
    std::vector<uint64_t> ptes;

    for (uint64_t pfn_idx = pfn_start; pfn_idx < pfn_end; ++ pfn_idx) {
      // Read a full page of data.
      std::vector<uint8_t> page_data(page_size_);

      if (addr_is_phys_) {
        read_page_phys(pfn_idx, page_data);
      }
      else {
        uint64_t pte;
        read_page_virt(pfn_idx, page_data, pte);
        ptes.push_back(pte);
      }

      // Append intersection of page and region to buffer.
      uint64_t page_start = pfn_idx * page_size_;
      uint64_t page_end = page_start + page_size_;
      uint64_t subpage_start = std::max(page_start, region_start_) - page_start;
      uint64_t subpage_end = std::min(page_end, region_end) - page_start;

      region_data.insert(
        region_data.end(),
        page_data.begin() + subpage_start,
        page_data.begin() + subpage_end
      );
    }

    // Break down address range into rows for display.
    uint64_t row_size = 0x10;
    uint64_t row_idx_end = (region_size_ + (row_size - 1)) / row_size;
    uint64_t prev_pte_idx = ptes.size(); // sentinel

    for (uint64_t row_idx = 0; row_idx < row_idx_end; ++ row_idx) {
      uint64_t row_start = region_start_ + (row_idx * row_size);

      if (addr_is_phys_ == false) {
        // Display the PTE at page boundaries.
        uint64_t pte_idx = (row_start / page_size_) - pfn_start;

        if (pte_idx != prev_pte_idx) {
          prev_pte_idx = pte_idx;
          std::cout << "PTE: 0x" << std::hex << ptes[pte_idx] << "\n";
        }
      }

      // Display the starting address of the row.
      std::cout << "0x" << std::hex << row_start << ": ";

      // Break the row into 4-byte columns for display.
      uint64_t cols_per_row = row_size / sizeof(uint32_t);
      uint64_t col_idx_end = std::min(cols_per_row, (region_end - row_start) / sizeof(uint32_t));

      for (uint64_t col_idx = 0; col_idx < col_idx_end; ++ col_idx) {
        uint64_t region_data_idx = (row_idx * cols_per_row + col_idx) * sizeof(uint32_t);
        uint32_t value = *(uint32_t *)&region_data[region_data_idx];
        std::cout << "      0x" << std::hex << std::setw(8) << std::setfill('0') << value;
      }

      std::cout << "\n";
    }
  }

  void read_page_phys(uint64_t pfn,
                      std::vector<uint8_t> &out_data)
  {
    kmemspy_read_page_phys_args args{};
    args.pfn_phys = pfn;
    args.data_buf = uintptr_t(out_data.data());

    mAssert(ioctl(dev_fd_, KMEMSPY_IOC_READ_PAGE_PHYS, &args) == 0,
      "failed to read physical page at 0x%" PRIx64, (pfn * page_size_));
  }

  void read_page_virt(uint64_t pfn,
                      std::vector<uint8_t> &out_data,
                      uint64_t &out_pte)
  {
    kmemspy_read_page_virt_args args{};
    args.pfn_virt = pfn;
    args.pid = pid_;
    args.data_buf = uintptr_t(out_data.data());

    mAssert(ioctl(dev_fd_, KMEMSPY_IOC_READ_PAGE_VIRT, &args) == 0,
      "failed to read virtual page at 0x%" PRIx64, (pfn * page_size_));

    out_pte = args.pte;
  }

protected:
  uint32_t pid_;
  bool addr_is_phys_;
  uint64_t region_start_;
  uint64_t region_size_;
  int dev_fd_;
  uint32_t page_size_;
};

int main(int argc,
         char *argv[])
{
  try {
    Application app(argc, argv);
    app.run();
  } catch (std::exception &ex) {
    std::cerr << ex.what() << "\n";
    std::exit(1);
  }
}
