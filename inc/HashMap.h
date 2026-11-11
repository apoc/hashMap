#ifndef HASH_MAP_H_
#define HASH_MAP_H_

#include "HashBucket.h"
#include <atomic>
#include <cstdint>
#include <functional>
#include <iostream>
#include <mutex>
#include <stdexcept>

constexpr size_t HASH_SIZE_DEFAULT = 2048;
constexpr size_t HASH_MAX_SIZE = 1u << 30; // safety cap for growth

namespace CTSL // Concurrent Thread Safe Library
{
// The class representing the hash map.
// It is expected for user defined types, the hash function will be provided.
// By default, the std::hash function will be used
// If the hash size is not provided, then a default size of 2048 will be used.
// The hash size must be a power of 2 and non-zero; it is the INITIAL size:
// the map grows transparently (doubling) when the average chain length
// exceeds the growth threshold. The bucket index is computed as hash & (hashSize - 1),
// so only the low bits of the hash value are used for bucketing.
// The hash table itself consists of an array of hash buckets.
// Each hash bucket is implemented as singly linked list which may be empty; there is no dummy head node.
// Locks are taken per bucket, hence multiple threads can write simultaneously in different buckets in the hash map.
// The table pointer/size is guarded by a table-level mutex: lookups take it for the brief
// dispatch only; a resize holds it exclusively while nodes are re-linked.
template <typename K, typename V, typename HASH = std::hash<K>> class HashMap
{
public:
  HashMap(size_t hashSize_ = HASH_SIZE_DEFAULT) : hashTable(nullptr), hashSize(0), nodeCount(0)
  {
    if (hashSize_ == 0 || (hashSize_ & (hashSize_ - 1)) != 0)
    {
      throw std::invalid_argument("CTSL::HashMap: hash size must be a power of 2 and non-zero");
    }

    hashSize = hashSize_;
    hashTable = new HashBucket<K, V>[hashSize_];
  }

  ~HashMap() // precondition: no concurrent operations (as with std:: containers)
  {
    delete[] hashTable;
  }
  // Copy and Move of the HashMap are not supported at this moment
  HashMap(const HashMap&) = delete;
  HashMap(HashMap&&)      = delete;
  HashMap& operator=(const HashMap&) = delete;
  HashMap& operator=(HashMap&&) = delete;

  // Function to find an entry in the hash map matching the key.
  // If key is found, the corresponding value is copied into the parameter "value" and function returns true.
  // If key is not found, function returns false.
  bool find(const K &key, V &value) const
  {
    std::lock_guard<std::mutex> tableLock(tableMutex_);
    const size_t hashValue = HASH()(key) & (hashSize - 1); // Only for hash size power of 2
    return hashTable[hashValue].find(key, value);
  }

  // Function to insert into the hash map.
  // If key already exists, update the value, else insert a new node in the bucket with the <key, value> pair.
  void insert(const K &key, const V &value)
  {
    bool added = false;
    {
      std::lock_guard<std::mutex> tableLock(tableMutex_);
      const size_t hashValue = HASH()(key) & (hashSize - 1); // Only for hash size power of 2
      added = hashTable[hashValue].insert(key, value);
    }
    if (added && nodeCount.fetch_add(1, std::memory_order_relaxed) + 1 > hashSize * GROWTH_THRESHOLD)
    {
      grow();
    }
  }

  // Function to remove an entry from the bucket, if found
  void erase(const K &key)
  {
    std::lock_guard<std::mutex> tableLock(tableMutex_);
    const size_t hashValue = HASH()(key) & (hashSize - 1); // Only for hash size power of 2
    if (hashTable[hashValue].erase(key))
    {
      nodeCount.fetch_sub(1, std::memory_order_relaxed);
    }
  }

  // Function to clean up the hasp map, i.e., remove all entries from it
  void clear()
  {
    std::lock_guard<std::mutex> tableLock(tableMutex_);
    for (size_t i = 0; i < hashSize; i++)
    {
      (hashTable[i]).clear();
    }
    nodeCount.store(0, std::memory_order_relaxed);
  }

  // Current number of buckets. Note: grows transparently over the lifetime of the map.
  size_t bucketCount() const
  {
    std::lock_guard<std::mutex> tableLock(tableMutex_);
    return hashSize;
  }

  // Current number of stored entries.
  size_t size() const { return nodeCount.load(std::memory_order_relaxed); }

private:
  static constexpr size_t GROWTH_THRESHOLD = 4; // grow when average chain exceeds this

  // Double the bucket count and re-link all nodes. Holds the table mutex
  // exclusively: no other thread can dispatch into a bucket, so the chains
  // can be re-linked without coordination. The bucket mutexes taken by
  // stealEntries/adoptEntry are for consistency of the HashBucket interface.
  void grow()
  {
    std::lock_guard<std::mutex> tableLock(tableMutex_);

    // Re-check under the lock: another thread may have grown already,
    // or the count may have dropped below the threshold.
    if (hashSize >= HASH_MAX_SIZE || nodeCount.load(std::memory_order_relaxed) <= hashSize * GROWTH_THRESHOLD)
    {
      return;
    }

    const size_t newHashSize = hashSize * 2;
    HashBucket<K, V> *newTable = new HashBucket<K, V>[newHashSize];

    for (size_t i = 0; i < hashSize; i++)
    {
      HashNode<K, V> *node = hashTable[i].stealEntries();
      while (node)
      {
        HashNode<K, V> *next = node->next;
        const size_t hashValue = HASH()(node->getKey()) & (newHashSize - 1);
        newTable[hashValue].adoptEntry(node);
        node = next;
      }
    }

    delete[] hashTable;
    hashTable = newTable;
    hashSize = newHashSize;
  }

  HashBucket<K, V> *hashTable;   // guarded by tableMutex_
  size_t hashSize;               // guarded by tableMutex_
  std::atomic<size_t> nodeCount; // number of stored entries
  mutable std::mutex tableMutex_;
};
} // namespace CTSL
#endif
