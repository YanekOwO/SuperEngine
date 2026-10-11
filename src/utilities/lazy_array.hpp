// This file is part of RLVM, a RealLive virtual machine clone.
//
// -----------------------------------------------------------------------
//
// Copyright (C) 2024 Serina Sakurai
// Copyright (C) 2006, 2007 Elliot Glaysher
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
//
// -----------------------------------------------------------------------

#pragma once

#include <boost/iterator/iterator_facade.hpp>
#include <boost/serialization/split_member.hpp>

#include <concepts>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <vector>

/**
 * @brief A container that implements an array where all objects are lazily
 *        evaluated when first accessed.
 *
 * For example, is the user (in the average case) *really* going to
 * use all 256 object slots? It's much more efficient use of memory to
 * lazily allocate the object array.
 *
 * Testing with CLANNAD shows use of 90 objects allocated in the foreground
 * layer at exit. Planetarian leaves 3 objects allocated. Kanon leaves 10.
 *
 * @tparam T The type of elements stored in the lazy array.
 */
template <typename T>
class LazyArray {
 public:
  /**
   * @brief Iterator to traverse all elements, whether allocated or not.
   */
  class full_iterator
      : public boost::
            iterator_facade<full_iterator, T, boost::forward_traversal_tag> {
   public:
    full_iterator(int pos, LazyArray<T>* array)
        : current_position_(pos), array_(array) {}

    /**
     * @brief Checks if the current item has been allocated.
     *
     * @return true if the current item is allocated, false otherwise.
     */
    bool valid() const { return array_->Exists(current_position_); }

    /**
     * @brief Returns the current position of the iterator.
     *
     * @return Current position as size_t.
     */
    size_t pos() const { return current_position_; }

   private:
    friend class boost::iterator_core_access;

    bool equal(full_iterator const& other) const {
      return current_position_ == other.current_position_ &&
             array_ == other.array_;
    }

    void increment() { current_position_++; }

    T& dereference() const { return (*array_)[current_position_]; }

    size_t current_position_;
    LazyArray<T>* array_;
  };

  /**
   * @brief Iterator to traverse only allocated elements.
   */
  class alloc_iterator
      : public boost::
            iterator_facade<alloc_iterator, T, boost::forward_traversal_tag> {
   public:
    explicit alloc_iterator(int pos, LazyArray<T>* array)
        : current_position_(pos), array_(array) {
      increment_to_valid();
    }

    /**
     * @brief Returns the current position of the iterator.
     *
     * @return Current position as size_t.
     */
    size_t pos() const { return current_position_; }

   private:
    friend class boost::iterator_core_access;

    bool equal(alloc_iterator const& other) const {
      return current_position_ == other.current_position_ &&
             array_ == other.array_;
    }

    void increment() {
      current_position_++;
      increment_to_valid();
    }

    void increment_to_valid() {
      while (current_position_ < array_->Size() &&
             !array_->Exists(current_position_))
        ++current_position_;
    }

    T& dereference() const { return array_->operator[](current_position_); }

    size_t current_position_;
    LazyArray<T>* array_;
  };

 public:
  LazyArray(size_t size) : arr_(size) {};

  LazyArray() = default;
  ~LazyArray() = default;

  LazyArray(const LazyArray&)
    requires std::is_copy_constructible_v<std::optional<T>>
  = default;
  LazyArray(const LazyArray&)
    requires(!std::is_copy_constructible_v<std::optional<T>>)
  = delete;

  LazyArray& operator=(const LazyArray&)
    requires std::is_copy_constructible_v<std::optional<T>>
  = default;
  LazyArray& operator=(const LazyArray&)
    requires(!std::is_copy_constructible_v<std::optional<T>>)
  = delete;

  LazyArray(LazyArray&&) noexcept
    requires std::movable<std::optional<T>>
  = default;
  LazyArray(LazyArray&&)
    requires(!std::movable<std::optional<T>>)
  = delete;

  LazyArray& operator=(LazyArray&&) noexcept
    requires std::movable<std::optional<T>>
  = default;
  LazyArray& operator=(LazyArray&&)
    requires(!std::movable<std::optional<T>>)
  = delete;

  /**
   * @brief Access or lazy-initializes an element at the specified position.
   *
   * @param pos Position of the element to access.
   * @return Reference to the element at the specified position.
   */
  T& operator[](size_t pos) {
    if (pos >= Size())
      throw std::out_of_range("LazyArray::operator[] out of range: " +
                              std::to_string(pos));

    if (!arr_[pos].has_value())
      arr_[pos] = T();
    return arr_[pos].value();
  }

  /**
   * @brief Returns a const reference to the element at the specified position.
   *
   * @param pos Position of the element.
   * @return const std::optional<T>& Optional containing the element if it
   * exists.
   */
  const std::optional<T>& At(size_t pos) const {
    if (pos >= Size())
      throw std::out_of_range("LazyArray::operator[] out of range: " +
                              std::to_string(pos));
    return arr_[pos];
  }

  /**
   * @brief Returns the size of the array.
   *
   * @return size_t The number of possible elements in the array.
   */
  size_t Size() const { return arr_.size(); }

  /**
   * @brief Changes the number of available slots in the array.
   *
   * Existing values are preserved when possible. Newly added slots remain
   * unallocated.
   *
   * @param size New number of available slots.
   */
  void Resize(size_t size) { arr_.resize(size); }

  /**
   * @brief Checks if an element at the specified index exists.
   *
   * @param index Position of the element to check.
   * @return true if an element at the specified index is initialized, false
   * otherwise.
   */
  bool Exists(size_t index) const { return arr_[index].has_value(); }

  /**
   * @brief Deletes the element at the specified index if it exists.
   *
   * @param index Position of the element to delete.
   */
  void DeleteAt(size_t index) { arr_[index].reset(); }

  /**
   * @brief Clears the array, resetting all elements. The size of the array is
   * maintained.
   */
  void Clear() {
    for (auto& it : arr_)
      it.reset();
  }

  /**
   * @brief Returns an iterator to the beginning of the array (full iteration).
   *
   * @return full_iterator Iterator to the first element.
   */
  full_iterator fbegin() { return full_iterator(0, this); }
  /**
   * @brief Returns an iterator to the end of the array (full iteration).
   *
   * @return full_iterator Iterator to the element past the last element.
   */
  full_iterator fend() { return full_iterator(Size(), this); }

  /**
   * @brief Returns an iterator to the beginning of allocated elements.
   *
   * @return alloc_iterator Iterator to the first allocated element.
   */
  alloc_iterator begin() { return alloc_iterator(0, this); }
  /**
   * @brief Returns an iterator to the end of allocated elements.
   *
   * @return alloc_iterator Iterator to the element past the last allocated
   * element.
   */
  alloc_iterator end() { return alloc_iterator(Size(), this); }

 private:
  std::vector<std::optional<T>> arr_;

  friend class boost::serialization::access;
  template <class Archive>
  void load(Archive& ar, unsigned int version) {
    int magic;

    ar & magic;
    if (magic >= 0) {
      throw std::runtime_error(
          "LazyArray: Error reading from outdated archive.");
    }

    size_t size, count;
    ar & size & count;
    arr_.resize(size);

    while (count--) {
      size_t pos;
      T value;
      ar & pos & value;
      arr_[pos] = std::move(value);
    }
  }

  template <class Archive>
  void save(Archive& ar, unsigned int version) const {
    // we do not directly serialize the array to decouple serialization from
    // data layout, considering possible future extension to use dynamically
    // allocated data structures.
    static constexpr int magic = -1;
    ar & magic;

    ar & arr_.size();
    size_t count = 0;
    for (const auto& it : arr_)
      if (it.has_value())
        ++count;
    ar & count;

    for (size_t i = 0; i < arr_.size(); ++i) {
      if (!arr_[i].has_value())
        continue;
      ar & i& arr_[i].value();
    }
  }

  BOOST_SERIALIZATION_SPLIT_MEMBER()
};

template <typename T>
using FullLazyArrayIterator = typename LazyArray<T>::full_iterator;
template <typename T>
using AllocatedLazyArrayIterator = typename LazyArray<T>::alloc_iterator;
