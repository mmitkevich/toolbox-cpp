#pragma once

#include <vector>
#include <memory>
#include <cassert>

//#include "toolbox/sys/Log.hpp"

namespace toolbox { 
inline namespace util {

template<typename T>
class Pool {
    struct Node {
        Node *next = nullptr;
        T value;
    };
    using SlabPtr = std::unique_ptr<Node[]>;
public:
    using value_type = T;
    constexpr static size_t Overhead = 16;
    constexpr static size_t PageSize = 4096;
    constexpr static size_t SlabSize = (PageSize - Overhead) / sizeof(T);
public:
    Pool() = default;

    // Copy.
    Pool(const Pool&) = delete;
    Pool& operator=(const Pool&) = delete;

    // Move.
    Pool(Pool&&) = delete;
    Pool& operator=(Pool&&) = delete;

    template<typename ...ArgsT>
    T* alloc(ArgsT... args) {
        Node* node;

        if (free_) {
            //TOOLBOX_DEBUG << "alloc using freelist";
            // Pop next free element from stack.
            node = free_;
            free_ = free_->next;
        } else {
            //TOOLBOX_DEBUG << "alloc using new slab "<<SlabSize;

            // Add new slab of timers to stack.
            SlabPtr slab{new Node[SlabSize]};
            node = &slab[0];

            for (size_t i = 1; i < SlabSize; ++i) {
                slab[i].next = free_;
                free_ = &slab[i];
            }
            slabs_.push_back(std::move(slab));
        }
        T* result = new(&node->value) T(std::forward<ArgsT>(args)...);
        return result;
    }
    
    void dealloc(T* e) noexcept {
        assert(e!=nullptr);
        Node* node = reinterpret_cast<Node*>(reinterpret_cast<char*>(e) - sizeof(Node*));
        node->next = free_;
        free_ = node;
    }

  private:
    std::vector<SlabPtr> slabs_;
    /// Head of free-list.
    Node* free_{nullptr};
};

}} // namespace ft::util

