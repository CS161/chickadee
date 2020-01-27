#ifndef CHICKADEE_LIST_HH
#define CHICKADEE_LIST_HH
#include "types.h"

struct list_links {
    list_links* next_ = nullptr;
    list_links* prev_ = nullptr;


    // Initialize an empty `list_links`
    list_links() = default;
    NO_COPY_OR_ASSIGN(list_links);

    // Reset this `list_links` to empty
    inline void reset();

    // Return true iff this `list_links` is linked in to some list
    inline bool is_linked() const;
    // Synonym for `!is_linked()`
    inline bool empty() const;
    // Remove this `list_links` from its containing list
    inline void erase();
    // Insert this `list_links` in a list immediately before `position`
    inline void insert_before(list_links* position);
};


template <typename T, list_links (T::* member)>
struct list {
    list_links head_;


    // Construct an empty list
    inline list();
    // Reset this list to empty, ignoring its current contents
    inline void reset();

    // Return true iff list is empty
    inline constexpr bool empty() const;

    // Return head of list (nullptr if empty)
    inline T* front() const;
    // Return tail of list (nullptr if empty)
    inline T* back() const;
    // Return list item after `x` (nullptr if last)
    inline constexpr T* next(T* x) const;
    // Return list item previous to `x` (nullptr if first)
    inline constexpr T* prev(T* x) const;

    // Push `x` onto head of list
    inline void push_front(T* x);
    // Remove and return head of list (return nullptr if empty)
    inline T* pop_front();

    // Push `x` onto tail of list
    inline void push_back(T* x);
    // Remove and return tail of list (return nullptr if empty)
    inline T* pop_back();

    // Remove `x` from list
    inline void erase(T* x);
    // Insert `x` immediately before `position`.
    // If `position == nullptr`, insert at tail
    inline void insert(T* position, T* x);

    // Swap the contents of this list with those of `other`.
    inline void swap(list<T, member>& other);


private:
    static T* from_links(list_links* ll) {
        return mem_container(ll, member);
    }
};


inline void list_links::reset() {
    next_ = prev_ = nullptr;
}

inline bool list_links::is_linked() const {
    return next_ != nullptr;
}

inline bool list_links::empty() const {
    return !is_linked();
}

inline void list_links::erase() {
    assert(next_ && prev_);
    prev_->next_ = next_;
    next_->prev_ = prev_;
    reset();
}

inline void list_links::insert_before(list_links* position) {
    assert(position->next_ && position->prev_);
    assert(!next_ && !prev_);
    prev_ = position->prev_;
    next_ = position;
    position->prev_->next_ = this;
    position->prev_ = this;
}


template <typename T, list_links (T::* member)>
inline list<T, member>::list() {
    reset();
}

template <typename T, list_links (T::* member)>
inline void list<T, member>::reset() {
    head_.next_ = head_.prev_ = &head_;
}

template <typename T, list_links (T::* member)>
inline constexpr bool list<T, member>::empty() const {
    return head_.next_ == &head_;
}

template <typename T, list_links (T::* member)>
inline T* list<T, member>::front() const {
    return empty() ? nullptr : from_links(head_.next_);
}

template <typename T, list_links (T::* member)>
inline T* list<T, member>::back() const {
    return empty() ? nullptr : from_links(head_.prev_);
}

template <typename T, list_links (T::* member)>
inline constexpr T* list<T, member>::next(T* x) const {
    return (x->*member).next_ == &head_
        ? nullptr : from_links((x->*member).next_);
}

template <typename T, list_links (T::* member)>
inline constexpr T* list<T, member>::prev(T* x) const {
    return (x->*member).prev_ == &head_
        ? nullptr : from_links((x->*member).prev_);
}

template <typename T, list_links (T::* member)>
inline void list<T, member>::push_front(T* x) {
    (x->*member).insert_before(head_.next_);
}

template <typename T, list_links (T::* member)>
inline T* list<T, member>::pop_front() {
    T* x = front();
    if (x) {
        erase(x);
    }
    return x;
}

template <typename T, list_links (T::* member)>
inline void list<T, member>::push_back(T* x) {
    (x->*member).insert_before(&head_);
}

template <typename T, list_links (T::* member)>
inline T* list<T, member>::pop_back() {
    T* x = back();
    if (x) {
        erase(x);
    }
    return x;
}

template <typename T, list_links (T::* member)>
inline void list<T, member>::erase(T* x) {
    (x->*member).erase();
}

template <typename T, list_links (T::* member)>
inline void list<T, member>::insert(T* position, T* x) {
    (x->*member).insert_before(position ? &head_ : &(position->*member));
}

template <typename T, list_links (T::* member)>
inline void list<T, member>::swap(list<T, member>& other) {
    list_links x, *p1, *p2;
    p1 = head_.next_;
    p2 = head_.prev_;
    p1->prev_ = p2->next_ = &other.head_;
    p1 = other.head_.next_;
    p2 = other.head_.prev_;
    p1->prev_ = p2->next_ = &head_;
    memcpy(&x, &head_, sizeof(x));
    memcpy(&head_, &other.head_, sizeof(x));
    memcpy(&other.head_, &x, sizeof(x));
}

#endif
