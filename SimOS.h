#include <iostream>
#include <vector>
#include <queue>
#include <algorithm>

struct FileReadRequest
{
  int PID{0};
  std::string fileName{""};
};

struct MemoryItem
{
  unsigned long long itemAddress;
  unsigned long long itemSize;
  int PID; // PID of the process using this chunk of memory
};

using MemoryUsage = std::vector<MemoryItem>;

class SimOS
{
public:
  /*
   * Constructor
   * The parameters specify number of hard disks in the simulated computer and amount of memory.
   * Disks enumeration starts from 0.
   */
  SimOS(int numberOfDisks, unsigned long long amountOfRAM): num_disks(numberOfDisks), ram_size(amountOfRAM), current_PID(0)
  {
    MemoryItem free_block;
    free_block.itemAddress = 0;
    free_block.itemSize = amountOfRAM;
    free_block.PID = -1;
    memory.push_back(free_block);

    // initialize the disk queues and disk_in_use vectors with empty values
    for(int i = 0; i < num_disks; ++i){
      disk_queues.push_back(std::queue<FileReadRequest>());
      disk_in_use.push_back(false);
    }
  }

  /*
   * Creates a new process with the specified priority in the mock system.
   * A new process occurs in a ready queue or starts using the CPU immediately.
   */
  bool NewProcess(int priority, unsigned long long size){
    // Check if there is sufficient free memory in the system
    bool memory_available = false;
    for(const auto& item: memory){
      if(item.PID == -1 && item.itemSize >= size){
        memory_available = true;
        break;
      }
    }
    if(!memory_available){
      // No free block was found
      return false;
    }

    // Allocate memory for the new process
    allocateMemory(current_PID + 1, size);

    // Add the new process to the ready queue
    ready_queue.push_back(current_PID + 1);

    // Increment the current PID
    ++current_PID;

    return true;
  }

  /*
   * The currently running process derives a child process.
   * The child's priority and size are inherited from the parent.
   * The child is placed at the end of the ready queue.
   */
  bool SimFork(){
    // Check if there is available memory for the new process
    bool memory_available = false;
    for(const auto& item: memory){
      if(item.PID == -1 && item.itemSize >= memory[current_PID].itemSize){
        memory_available = true;
        break;
      }
    }
    if(!memory_available){
      // No free block was found
      return false;
    }

    // Allocate memory for the new process
    allocateMemory(current_PID + 1, memory[current_PID].itemSize);

    // Create a new child process with the same priority and size as the parent process
    int new_PID = current_PID + 1;
    int priority = new_PID % 5 + 1;
    unsigned long long size = memory[current_PID].itemSize;
    MemoryItem memory_block = memory[current_PID];

    // Add the new process to the end of the ready queue
    ready_queue.push_back(new_PID);

    // Increment the current PID
    ++current_PID;

    return true;
  }

  /*
   * Processes that are currently using the CPU.
   * Make sure to free the memory used by this process immediately.
   * If the parent is already waiting, the process terminates immediately and the parent becomes runnable.
   */
  void SimExit(){
    // Step 1: Check if the current process is the root process
    if(current_PID == 0){
      return; // root process cannot exit
    }

    // Step 2: Release the memory used by the current process
    for(auto& item: memory){
      if(item.PID == current_PID){
        item.PID = -1;
        break;
      }
    }

    // Step 3: Check if the parent process is waiting for this process to exit
    int parent_PID = -1;
    for(const auto& item: memory){
      if(item.itemAddress == current_PID){
        parent_PID = item.PID;
        break;
      }
    }
    if(parent_PID != -1){
      // Parent process is waiting, so add it to the ready queue
      ready_queue.push_back(parent_PID);
    }
    else{
      // Step 4: Parent process is not waiting, so mark this process as zombie
      for(auto& item: memory){
        if(item.PID == current_PID){
          item.PID = -2;
          break;
        }
      }
    }

    // Step 5: Find all the child processes of this process and terminate them recursively
    std::vector<int> children;
    for(const auto& item: memory){
      if(item.PID == current_PID){
        children.push_back(item.itemAddress);
      }
    }
    for(const auto& child_PID: children){
      // Call SimExit recursively to terminate the child process and all its descendants
      current_PID = child_PID;
      SimExit();
    }

    // Step 6: Remove this process from the ready queue and select the next process to run
    ready_queue.erase(std::remove(ready_queue.begin(), ready_queue.end(), current_PID), ready_queue.end());
    scheduleNextProcess();
  }

  /*
   *
   */
  void SimWait(){
    bool zombie_exists = false;
    int zombie_PID = -1;
  
    // Search for any zombie child process
    for(const auto& item: memory){
      if(item.PID == current_PID && item.itemSize == 0){
        zombie_PID = item.PID;
        zombie_exists = true;
        break;
      }
    }
  
    if(zombie_exists){
      // If a zombie child process exists, remove it from the memory
      for(auto& item: memory){
        if(item.PID == zombie_PID){
          item.PID = -1;
          item.itemSize = ram_size;
          break;
        }
      }
    }
    else{
      // If no zombie child process exists, wait for a child process to terminate
      bool child_terminated = false;
      while(!child_terminated){
        for(auto it = memory.begin(); it != memory.end(); ++it){
          if(it->PID == current_PID && it->itemSize != 0){
            // A child process exists
            ++it->itemSize; // change the child process into a zombie process
            child_terminated = true;
            break;
          }
        }
      }
    }
  
    // Add the current process ID to the end of the ready queue
    ready_queue.push_back(current_PID);
  
    // Schedule the next process to run
    scheduleNextProcess();
  }

  /*
   *
   */
  void DiskReadRequest(int diskNumber, std::string fileName){
    // Check if the disk number is valid
  if (diskNumber < 0 || diskNumber >= num_disks) {
    std::cerr << "Error: Invalid disk number\n";
    return;
  }

  // Add the read request to the disk queue
  disk_queues[diskNumber].push({current_PID, fileName});

  // Stop the current process from using the CPU
  ready_queue.erase(std::remove(ready_queue.begin(), ready_queue.end(), current_PID), ready_queue.end());

  // Switch to the next process in the ready queue
  scheduleNextProcess();
  }

  /*
   *
   */
  void DiskJobCompleted(int diskNumber){
    // Check if the diskNumber is valid
    if(diskNumber < 0 || diskNumber >= num_disks){
      std::cout << "Invalid disk number!\n";
      return;
    }
  
    // Mark the disk as not in use
    disk_in_use[diskNumber] = false;

    // Check if there are any pending read requests for this disk
    if(!disk_queues[diskNumber].empty()){
      // Dequeue the first request
      auto request = disk_queues[diskNumber].front();
      disk_queues[diskNumber].pop();

      // Schedule the corresponding process to run
      ready_queue.push_back(request.PID);
      scheduleNextProcess();
    }
  }

  /*
   *
   */
  int GetCPU( ){
    return current_process;
  }

  /*
   *
   */
  std::vector<int> GetReadyQueue( ){
    return ready_queue;
  }

  /*
   *
   */
  MemoryUsage GetMemory( )
  {
    MemoryUsage memory_usage;

    // Iterate over the memory vector and create a MemoryItem for each block of memory in use by a process
    for (const auto& item : memory) {
      if (item.PID != -1 && item.PID != -2) {
        MemoryItem memory_item;
        memory_item.itemAddress = item.itemAddress;
        memory_item.itemSize = item.itemSize;
        memory_item.PID = item.PID;

        memory_usage.push_back(memory_item);
      }
    }

    // Sort the MemoryUsage vector by memory address
    std::sort(memory_usage.begin(), memory_usage.end(),
            [](const MemoryItem& a, const MemoryItem& b) { return a.itemAddress < b.itemAddress; });

    return memory_usage;
  }

  /*
   *
   */
  FileReadRequest GetDisk(int diskNumber){
    if (diskNumber >= num_disks) {
      // invalid disk number, return default FileReadRequest
      return FileReadRequest{};
    }

    if (disk_in_use[diskNumber]) {
      // disk is not idle, return the next file read request in the queue
      auto& queue = disk_queues[diskNumber];
      auto request = queue.front();
      queue.pop();
      return request;
    } 
    else {
      // disk is idle, return default FileReadRequest
      return FileReadRequest{};
    }
  }

  /*
   *
   */
  std::queue<FileReadRequest> GetDiskQueue(int diskNumber){
    // Check if the specified disk is in use
    if (!disk_in_use[diskNumber])
    {
      std::cout << "Disk " << diskNumber << " is not in use.\n";
      return std::queue<FileReadRequest>(); // return an empty queue
    }

    // Return the I/O-queue of the specified disk
    return disk_queues[diskNumber];
  }

private:
  int num_disks;
  int ram_size;
  int current_process;

  int current_PID; // private member variable to keep track of the current process ID

  std::vector<int> ready_queue; // private member variable to hold the IDs of processes waiting to be executed

  std::vector<int> blocked_queue; // private member variable to hold the IDs of processes waiting for I/O operations to complete

  std::vector<MemoryItem> memory; // private member variable to keep track of the memory usage in the system

  std::vector<std::queue<FileReadRequest>> disk_queues; // private member variable to hold queues of I/O requests for each disk

  std::vector<bool> disk_in_use; // private member variable to indicate whether a disk is currently busy with an I/O operation
  
  // private member function to select the next process to run from the ready queue
  void scheduleNextProcess(){
    // Find the next process with the highest priority
    int next_PID = -1;
    int highest_priority = 0;
    for(const auto& pid: ready_queue){
      for(const auto& item: memory){
        if(item.PID == pid && item.itemSize != 0 && pid != current_process){
          // Only consider processes that have memory allocated and are not the current process
          if(pid % 5 + 1 > highest_priority){
            highest_priority = pid % 5 + 1;
            next_PID = pid;
          }
          break;
        }
      }
    }

    // Set the next process as the current process
    current_process = next_PID;
  }


  // private member function to allocate memory for a process
  void allocateMemory(int PID_, unsigned long long size){
    // Find a free memory block and assign it to the process
    for(auto& item: memory){
      if(item.PID == -1 && item.itemSize >= size){
        item.PID = PID_;
        item.itemSize = size;
        break;
      }
    }

    // Set the new process as the current process
    current_process = PID_;
  }



  // private member function to free memory used by a process
  void deallocateMemory(int PID){
    for(auto it = memory.begin(); it != memory.end(); ++it){
      if(it->PID == PID){
        it->PID = -1;
        // Check if the adjacent blocks are also free
        if(it != memory.begin() && (it - 1)->PID == -1){
          // Merge with the previous block
          (it - 1)->itemSize += it->itemSize;
          memory.erase(it);
          it = it - 1;
        }
        if(it != memory.end() - 1 && (it + 1)->PID == -1){
          // Merge with the next block
          it->itemSize += (it + 1)->itemSize;
          memory.erase(it + 1);
        }
        return;
      }
    }
  }



  // private member function to handle completion of a disk I/O operation
  void handleDiskInterrupt(int diskNumber){
    // Check if there is a read request waiting to be completed for the given disk
    if (!disk_queues[diskNumber].empty()) {
      // Get the process ID and file name from the front of the disk queue
      int PID = disk_queues[diskNumber].front().PID;
      std::string fileName = disk_queues[diskNumber].front().fileName;

      // Remove the read request from the front of the disk queue
      disk_queues[diskNumber].pop();

      // Set the disk as not in use
      disk_in_use[diskNumber] = false;

      // Allocate memory for the process
      allocateMemory(PID, ram_size);

      // Add the process to the ready queue
      ready_queue.push_back(PID);

      // Schedule the next process to run
      scheduleNextProcess();

      // Output a message indicating that the read operation has completed
      std::cout << "Disk " << diskNumber << " has completed reading " << fileName << " for process " << PID << std::endl;
    }
  }



  //  private member function to handle memory allocation failure due to insufficient memory
  void handleMemoryInterrupt(){
    if(!ready_queue.empty()){
      // Get the PID of the process at the front of the ready queue
      int PID_to_kill = ready_queue.front();
      // Remove the process at the beginning of ready queue
      ready_queue.erase(ready_queue.begin());
      // deallocate the memory used by the process
      deallocateMemory(PID_to_kill);
    }
    else{
      // There are no processes waiting in the ready queue that can be killed to free up memory
      // Throw an exception or print an error message
      throw std::runtime_error("Out of memory");
    }
  }
};
