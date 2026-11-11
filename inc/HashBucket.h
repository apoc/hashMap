#ifndef HASH_BUCKET_H_
#define HASH_BUCKET_H_

#include "HashNode.h"
#include <shared_mutex>
#include <mutex>

namespace CTSL // Concurrent Thread Safe Library
{
// Class representing a hash bucket. The bucket is implemented as a singly linked list.
// The list may be empty; the head node is a real entry (no dummy head node).
template <typename K, typename V> class HashBucket
{
public:
  HashBucket() : head(nullptr)
  {
  }

  ~HashBucket() // delete the bucket
  {
    std::unique_lock<std::shared_timed_mutex> lock(mutex_); // take a lock before removing the nodes

    while (head)
    {
      HashNode<K, V> *next = head->next;
      delete head;

      head = next;
    }
  }

  // Function to find an entry in the bucket matching the key
  // If key is found, the corresponding value is copied into the parameter "value" and function returns true.
  // If key is not found, function returns false
  bool find(const K &key, V &value) const
  {
    // A shared mutex is used to enable mutiple concurrent reads
    std::shared_lock<std::shared_timed_mutex> lock(mutex_);
    HashNode<K, V> *node = head;

    while (node != nullptr)
    {
      if (node->getKey() == key)
      {
        value = node->getValue();
        return true;
      }
      node = node->next;
    }
    return false;
  }

  // Function to insert into the bucket
  // If key already exists, update the value, else insert a new node in the bucket with the <key, value> pair
  void insert(const K &key, const V &value)
  {
    // Exclusive lock to enable single write in the bucket
    std::unique_lock<std::shared_timed_mutex> lock(mutex_);
    HashNode<K, V> **node = &head;

    while (*node)
    {
      if ((*node)->getKey() == key)
      {
        (*node)->setValue(value); // Key found in bucket, update the value
        return;
      }
      node = &((*node)->next);
    }

    // now node points to the last element's ->next (or head), so the new node can be linked unconditionally
    *node = new HashNode<K, V>(key, value);
  }

  // Function to remove an entry from the bucket, if found
  void erase(const K &key)
  {
    // Exclusive lock to enable single write in the bucket
    std::unique_lock<std::shared_timed_mutex> lock(mutex_);
    HashNode<K, V> *node = head;
    HashNode<K, V> **prev = &head;

    while (node)
    {
      if (node->getKey() == key)
      {
        *prev = node->next;
        delete node;
        return;
      }
      else
      {
        prev = &node->next;
        node = node->next;
      }
    }
  }

  // Function to clear the bucket
  void clear()
  {
    // Exclusive lock to enable single write in the bucket
    std::unique_lock<std::shared_timed_mutex> lock(mutex_);

    while (head)
    {
      HashNode<K, V> *next = head->next;
      delete head;

      head = next;
    }
  }

private:
  HashNode<K, V> *head = nullptr;         // The head node of the bucket
  mutable std::shared_timed_mutex mutex_; // The mutex for this bucket
};
} // namespace CTSL

#endif