#include <arpa/inet.h>
#include <mpi.h>
#include <rdma/fabric.h>
#include <rdma/fi_domain.h>
#include <rdma/fi_endpoint.h>

#include <cstring>
#include <fstream>
#include <iostream>
#include <regex>
#include <string>
#include <vector>

#include "hermes_shm/util/logging.h"

// Function to find IP address from fabric
std::string find_ip_from_fabric(const char* provider, const char* domain,
                                const char* fabric) {
  struct fi_info *hints, *info;
  hints = fi_allocinfo();
  hints->fabric_attr->prov_name = strdup(provider);
  hints->domain_attr->name = strdup(domain);

  int ret = fi_getinfo(FI_VERSION(1, 9), NULL, NULL, 0, hints, &info);
  if (ret) {
    fi_freeinfo(hints);
    return "";
  }

  std::string result;
  while (info) {
    if (info->src_addr && info->fabric_attr && info->fabric_attr->name) {
      // Check if fabric name matches the regex
      //   HILOG(kInfo, "{}", info->fabric_attr->name);
      std::string test_fabric(info->fabric_attr->name);
      if (test_fabric == fabric) {
        char ip[INET_ADDRSTRLEN];
        struct sockaddr_in* addr = (struct sockaddr_in*)info->src_addr;
        inet_ntop(AF_INET, &(addr->sin_addr), ip, INET_ADDRSTRLEN);
        result = ip;
        break;
      }
    }
    info = info->next;
  }

  fi_freeinfo(hints);
  fi_freeinfo(info);
  return result;
}

int main(int argc, char* argv[]) {
  if (argc != 5) {
    std::cerr << "Usage: " << argv[0]
              << " <provider> <domain> <fabric> <hostfile>" << std::endl;
    return 1;
  }

  MPI_Init(&argc, &argv);

  int rank, size;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &size);

  HILOG(kInfo, "Beginning to find ips: {}\n", rank);

  // Find IP address for this rank
  std::string ip = find_ip_from_fabric(argv[1], argv[2], argv[3]);

  // Gather all IPs to rank 0
  const int MAX_IP_LEN = 16;
  char* all_ips = nullptr;
  if (rank == 0) {
    all_ips = new char[size * MAX_IP_LEN];
  }

  char local_ip[MAX_IP_LEN];
  strncpy(local_ip, ip.c_str(), MAX_IP_LEN - 1);
  local_ip[MAX_IP_LEN - 1] = '\0';

  MPI_Gather(local_ip, MAX_IP_LEN, MPI_CHAR, all_ips, MAX_IP_LEN, MPI_CHAR, 0,
             MPI_COMM_WORLD);

  // Rank 0 saves to file
  if (rank == 0) {
    std::ofstream outfile(argv[4]);
    for (int i = 0; i < size; i++) {
      std::string curr_ip(all_ips + i * MAX_IP_LEN);
      if (!curr_ip.empty()) {
        outfile << curr_ip << std::endl;
      }
    }
    outfile.close();
    HILOG(kInfo, "Saving ips: {}\n", argv[4]);
    delete[] all_ips;
  }

  MPI_Finalize();
  return 0;
}