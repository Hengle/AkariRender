// MIT License
//
// Copyright (c) 2019 椎名深雪
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#pragma once

#include <cstring>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>
#include <optional>
//#include <list>

#include <akari/core/akari.h>
#include <akari/core/detail/typeinfo.hpp>
#include <akari/core/platform.h>

namespace akari {

    namespace detail {
        template <typename T> struct get_internal { using type = T; };
        template <typename T> struct get_internal<T *> { using type = T; };
        template <typename T> struct get_internal<std::shared_ptr<T>> { using type = T; };
        template <typename T> struct get_internal<std::reference_wrapper<T>> { using type = T; };
        template <typename T> using get_internal_t = typename get_internal<T>::type;

        template <typename T> struct is_shared_ptr : std::false_type {};
        template <typename T> struct is_shared_ptr<std::shared_ptr<T>> : std::true_type {};
        template <typename T> constexpr bool is_shared_ptr_v = is_shared_ptr<T>::value;

        template <typename T> struct is_sequential_container : std::false_type {};
        template <typename T> struct is_sequential_container<std::vector<T>> : std::true_type { using type = T; };
        //        template<typename T>struct is_sequential_container<std::list<T>>: std::true_type {};
        template <typename T> struct is_reference_wrapper : std::false_type {};
        template <typename T> struct is_reference_wrapper<std::reference_wrapper<T>> : std::true_type {};
        template <typename T> constexpr bool is_reference_wrapper_v = is_reference_wrapper<T>::value;
    } // namespace detail

    struct Type;
    struct Any;

    template <typename T> Any make_any(T value);

    template <typename T> Any make_any_ref(T &&value);

    struct SequentialContainerView;
    struct AssociativeContainerView;

    inline std::optional<void *> any_pointer_cast(TypeInfo to, TypeInfo from, void *p);

    inline std::optional<std::shared_ptr<void>> any_shared_pointer_cast(TypeInfo to, TypeInfo from,
                                                                        std::shared_ptr<void> p);

    struct Any {
        friend struct Type;

      private:
        struct Container {
            [[nodiscard]] virtual std::unique_ptr<Container> clone() const = 0;

            virtual void assign(void *) = 0;

            virtual void assign_underlying(void *) = 0;

            virtual void *get() = 0;

            virtual std::optional<std::shared_ptr<void>> get_shared() const = 0;

            virtual void assign_shared(std::shared_ptr<void>) = 0;

            virtual Any get_underlying() = 0;

            virtual std::optional<TypeInfo> get_underlying_type() = 0;

            virtual std::optional<SequentialContainerView> get_container_view() = 0;

            //            virtual std::optional<AssociativeContainerView> get_associative_container_view() = 0;
            virtual ~Container() = default;
        };

        template <typename Actual, typename T> struct ContainerImpl : Container {
            T value;

            explicit ContainerImpl(const T &_value) : value(_value) {}

            [[nodiscard]] std::unique_ptr<Container> clone() const override {
                return std::make_unique<ContainerImpl>(value);
            }

            void *get() override { return &value; }
            void assign(void *p) override { value = *reinterpret_cast<T *>(p); }
            void assign_underlying(void *p) override {
                if constexpr (detail::is_shared_ptr_v<Actual> || std::is_pointer_v<Actual>) {
                    if constexpr (detail::is_reference_wrapper_v<T>) {
                        auto &tmp = const_cast<Actual &>(static_cast<detail::get_internal_t<T> &>(value));
                        *tmp = *reinterpret_cast<detail::get_internal_t<Actual> *>(p);
                    } else {
                        *value = *reinterpret_cast<detail::get_internal_t<Actual> *>(p);
                    }

                } else {
                    throw std::runtime_error("assigning to underlying value of non-pointer");
                }
            }
            void assign_shared(std::shared_ptr<void> p) override {
                if constexpr (detail::is_shared_ptr_v<Actual>) {
                    if constexpr (detail::is_reference_wrapper_v<T>) {
                        auto &tmp = const_cast<Actual &>(static_cast<detail::get_internal_t<T> &>(value));
                        tmp = std::reinterpret_pointer_cast<detail::get_internal_t<Actual>>(p);
                    } else {
                        value = std::reinterpret_pointer_cast<detail::get_internal_t<Actual>>(p);
                    }

                } else {
                    throw std::runtime_error("assigning to non-shared_ptr");
                }
            }
            [[nodiscard]] Any get_underlying() override {
                if constexpr (detail::is_shared_ptr_v<Actual> || std::is_pointer_v<Actual>) {
                    if constexpr (detail::is_reference_wrapper_v<T>) {
                        auto &tmp = const_cast<Actual &>(static_cast<detail::get_internal_t<T> &>(value));
                        return make_any_ref(*tmp);
                    } else {
                        Actual &tmp = (value);
                        return make_any_ref(*tmp);
                    }
                }
                return Any();
            }
            Actual & get_actual()const {
                if constexpr (detail::is_reference_wrapper_v<T>) {
                    auto &tmp = const_cast<Actual &>(static_cast<detail::get_internal_t<T> &>(value));
                    return tmp;
                } else {
                    return const_cast<Actual &>(value);
                }
            }
            [[nodiscard]] inline std::optional<TypeInfo> get_underlying_type() override {
                if constexpr (detail::is_shared_ptr_v<Actual> || std::is_pointer_v<Actual>) {
                    if(get_actual() != nullptr)
                        return type_of(*get_actual());
                    else{
                        return type_of<detail::get_internal_t<Actual>>();
                    }
                }
                return {};
            }
            [[nodiscard]] std::optional<std::shared_ptr<void>> get_shared() const override {
                if constexpr (detail::is_shared_ptr_v<Actual>) {
                    return get_actual();
                } else {
                    return {};
                }
            }

            inline std::optional<SequentialContainerView> get_container_view() override;
            //            inline std::optional<AssociativeContainerView> get_associative_container_view() override;
        };

        template <typename A, typename T> std::unique_ptr<Container> make_container(T &&value) {
            return std::make_unique<ContainerImpl<A, T>>(value);
        }

      public:
        struct from_value_t {};
        struct from_ref_t {};

        Any() = default;

        template <typename T>
        Any(T value, std::enable_if_t<std::is_reference_v<T>, std::true_type> _ = {})
            : Any(from_ref_t{}, std::forward<T>(value)) {}

        template <typename T>
        Any(T value, std::enable_if_t<!std::is_reference_v<T>, std::true_type> _ = {})
            : Any(from_value_t{}, std::forward<T>(value)) {}

        template <typename T> Any(from_value_t _, T value) : type(type_of<T>()), kind(EValue) {
            _ptr = make_container<std::decay_t<T>, T>(std::move(value));
        }

        template <typename T, typename U = std::remove_reference_t<T>>
        Any(from_ref_t _, T &&value) : type(type_of<std::decay_t<T>>()), kind(ERef) {
            using R = std::reference_wrapper<U>;
            _ptr = make_container<std::decay_t<T>, R>(R(std::forward<T>(value)));
        }

        Any(const Any &rhs) : type(rhs.type), kind(rhs.kind) {
            if (rhs._ptr) {
                _ptr = rhs._ptr->clone();
            }
        }

        Any(Any &&rhs) noexcept : type(rhs.type), kind(rhs.kind) { _ptr = std::move(rhs._ptr); }

        Any &operator=(Any &&rhs) {
            type = rhs.type;
            kind = rhs.kind;
            _ptr = std::move(rhs._ptr);
            return *this;
        }

        Any &operator=(const Any &rhs) {
            if (&rhs == this)
                return *this;
            type = rhs.type;
            kind = rhs.kind;
            if (rhs._ptr) {
                _ptr = rhs._ptr->clone();
            }
            return *this;
        }

        template <typename T>[[nodiscard]] bool is_of() const {
            if (kind == EVoid) {
                return std::is_same_v<T, void>;
            }
            if constexpr (detail::is_shared_ptr_v<T>) {
                if(is_shared_pointer()){
                    std::shared_ptr<void> p = _ptr->get_shared().value();
                    if (auto opt = any_shared_pointer_cast(type_of<T>(), _ptr->get_underlying_type().value(), p)) {
                        return true;
                    }
                }
            }
            return type == type_of<T>();
        }

        [[nodiscard]] bool has_value() const { return _ptr != nullptr && kind != EVoid; }

        template <typename T> std::shared_ptr<T> shared_cast() const {
            if (!is_shared_pointer()) {
                throw std::runtime_error("Any is not of std::shared_ptr");
            }
            if (kind == EVoid) {
                throw std::runtime_error("Any is of void");
            }

            std::shared_ptr<void> p = _ptr->get_shared().value();
            if (auto opt = any_shared_pointer_cast(type_of<T>(), _ptr->get_underlying_type().value(), p)) {
                return std::reinterpret_pointer_cast<T>(opt.value());
            } else {
                throw std::runtime_error("bad Any::shared_cast<T>()");
            }
        }
        template <typename U, typename = std::enable_if<!std::is_reference_v<U>>> U as_value() const {
            if constexpr (detail::is_shared_ptr_v<U>) {
                return shared_cast<detail::get_internal_t<U>>();
            } else {
                return as<U>();
            }
        }
        template <typename U, typename T = std::remove_reference_t<U>> T &as() const {
            //            AKR_ASSERT(has_value());
            using P = std::conditional_t<std::is_const_v<T>, const void *, void *>;
            if (kind == EVoid) {
                throw std::runtime_error("Any is of void");
            }
            if (type != type_of<T>()) {

                P p;
                if (kind == ERef) {
                    using result_t = std::conditional_t<std::is_const_v<T>, std::reference_wrapper<const T>,
                                                        std::reference_wrapper<T>>;
                    auto raw = _ptr->get();
                    auto ref_wrapper = *reinterpret_cast<result_t *>(raw);
                    p = &ref_wrapper.get();
                } else {
                    p = _ptr->get();
                }
                if (auto opt = any_pointer_cast(type_of<T>(), type, const_cast<void *>(p))) {
                    return *reinterpret_cast<T *>(const_cast<P>(opt.value()));
                } else {
                    std::string msg = std::string("bad Any::as<T>(); T: ").append(typeid(T).name()).append("this: ").append(type.name.data());
                    throw std::runtime_error(msg);
                }

            } else {
                if (kind == ERef) {
                    using result_t = std::conditional_t<std::is_const_v<T>, std::reference_wrapper<const T>,
                                                        std::reference_wrapper<T>>;
                    auto raw = _ptr->get();
                    auto ref_wrapper = *reinterpret_cast<result_t *>(raw);
                    return ref_wrapper.get();
                } else {
                    auto raw = _ptr->get();
                    // std::cout <<"raw " << raw << std::endl;
                    return *reinterpret_cast<T *>(raw);
                }
            }
        }

        template <typename T> const T &as_const() const { return const_cast<Any *>(this)->as<T>(); }

        [[nodiscard]] bool is_pointer() const { return _ptr->get_underlying().has_value(); }

        [[nodiscard]] bool is_null() const { return _ptr->get_shared().value() == nullptr; }

        [[nodiscard]] bool is_shared_pointer() const { return _ptr->get_shared().has_value(); }

        [[nodiscard]] std::shared_ptr<void> _get_internal_shared_pointer() const { return _ptr->get_shared().value(); }

        [[nodiscard]] void *_get_internal_pointer() const { return _ptr.get(); }

        [[nodiscard]] Any get_underlying() const { return _ptr->get_underlying(); }

        [[nodiscard]] TypeInfo get_underlying_type() const { return _ptr->get_underlying_type().value(); }

        [[nodiscard]] inline std::optional<SequentialContainerView> get_sequential_container_view() const;

        [[nodiscard]] inline const TypeInfo &_get_type() const { return type; }
        [[nodiscard]] inline TypeInfo get_type() const {
            if (auto r = _ptr->get_underlying_type()) {
                return r.value();
            } else {
                return type;
            }
        }
        void set_value(const Any &value) const {
            if (is_shared_pointer()) {
                if (!value.is_shared_pointer()) {
                    throw std::runtime_error("assigning shared_ptr to non shared_ptr");
                }
                if (auto p = any_shared_pointer_cast(get_underlying_type(), value.get_underlying_type(),
                                                     value._get_internal_shared_pointer())) {
                    _ptr->assign_shared(p.value());
                }
            } else {
                if (type == value.type) {
                    _ptr->assign(value._get_internal_pointer());
                } else {
                    throw std::runtime_error("Bad Any::set_value");
                }
            }
        }

        void set_underlying(const Any &value) const {
            if (get_underlying_type() == value.get_underlying_type()) {
                if (value.is_shared_pointer()) {
                    _ptr->assign_underlying(value._get_internal_shared_pointer().get());
                } else {
                    _ptr->assign_underlying(value._get_internal_pointer());
                }

            } else {
                throw std::runtime_error("Bad Any::set_value");
            }
        }

      private:
        TypeInfo type;
        std::unique_ptr<Container> _ptr;
        enum Kind : uint8_t { EVoid, EValue, ERef };
        Kind kind = EVoid;
    };

    template <typename T> struct _AnyIterator {
        std::function<void(const T &)> inc, dec;
        std::function<T(const T &)> get;
        std::function<bool(const T &, const T &)> compare;
        T _data;

        T operator*() { return get(_data); }

        bool operator==(const _AnyIterator &rhs) const { return compare(_data, rhs._data); }

        bool operator!=(const _AnyIterator &rhs) const { return !compare(_data, rhs._data); }

        _AnyIterator &operator++() {
            inc(_data);
            return *this;
        }

        _AnyIterator &operator--() {
            dec(_data);
            return *this;
        }
    };

    using AnyIterator = _AnyIterator<Any>;
    template <typename T> struct _ContainerView {
        std::function<_AnyIterator<T>()> begin, end;
        std::function<_AnyIterator<T>(const _AnyIterator<T> &)> erase;
        std::function<void(const _AnyIterator<T> &, const T &)> insert;
    };
    struct SequentialContainerView : _ContainerView<Any> {};
    struct AssociativeContainerView : _ContainerView<std::pair<Any, Any>> {};

    template <typename T> Any make_any(T value) { return Any(Any::from_value_t{}, std::move(value)); }
    template <> inline Any make_any(const Any &value) { return Any(value); }
    template <> inline Any make_any(Any &value) { return Any(static_cast<const Any &>(value)); }
    template <> inline Any make_any(Any &&value) { return Any(value); }
    template <typename T> Any make_any_ref(T &&value) { return Any(Any::from_ref_t{}, std::forward<T>(value)); }
    template <> inline Any make_any_ref(const Any &value) { return Any(value); }
    template <> inline Any make_any_ref(Any &value) { return Any(static_cast<const Any &>(value)); }
    template <> inline Any make_any_ref(Any &&value) { return Any(value); }
    template <typename Actual, typename T>
    inline std::optional<SequentialContainerView> Any::ContainerImpl<Actual, T>::get_container_view() {
        if constexpr (!detail::is_sequential_container<Actual>::value) {
            return {};
        } else {
            // T is a container
            SequentialContainerView view;
            using Iter = typename Actual::iterator;
            auto make_iter = [](const Iter &iter) -> AnyIterator {
                AnyIterator any_iter;
                any_iter._data = make_any(iter);
                any_iter.get = [](const Any &data) { return make_any_ref(*data.as<Iter>()); };
                any_iter.inc = [](const Any &data) { ++data.as<Iter>(); };
                any_iter.dec = [](const Any &data) { --data.as<Iter>(); };
                any_iter.compare = [](const Any &a, const Any &b) { return a.as<Iter>() == b.as<Iter>(); };
                return any_iter;
            };
            using ElemT = typename detail::is_sequential_container<Actual>::type;
            Actual &vec = value;
            view.begin = [&]() { return make_iter(vec.begin()); };
            view.end = [&]() { return make_iter(vec.end()); };
            view.insert = [&](const AnyIterator &pos, const Any &v) {
                vec.insert(pos._data.as<Iter>(), v.as<ElemT>());
            };
            view.erase = [&](const AnyIterator &pos) { return make_iter(vec.erase(pos._data.as<Iter>())); };
            return view;
        }
    }

    inline std::optional<SequentialContainerView> Any::get_sequential_container_view() const {
        return _ptr->get_container_view();
    }

    template <typename... Args> struct TypeList {};
    template <size_t Idx, typename... Args> struct get_nth_type {};
    template <typename T> struct get_nth_type<0, TypeList<T>> { using type = T; };
    template <typename T, typename... Args> struct get_nth_type<0, TypeList<T, Args...>> { using type = T; };
    template <size_t Idx, typename T, typename... Args> struct get_nth_type<Idx, TypeList<T, Args...>> {
        using type = typename get_nth_type<Idx - 1, TypeList<Args...>>::type;
    };
    template <size_t Idx, typename Tuple> struct get_nth_element {
        using type = typename get_nth_type<Idx, Tuple>::type;
    };

    template <size_t Idx, typename Tuple> using nth_element_t = typename get_nth_element<Idx, Tuple>::type;

    struct Function {
      private:
        template <typename R, typename T> static Any _make_any_(T &&value) {
            if constexpr (std::is_reference_v<R>) {
                return make_any_ref(value);
            } else {
                return make_any(value);
            }
        }

        std::vector<TypeInfo> signature;

      public:
        Function() = default;

        using FunctionWrapper = std::function<Any(std::vector<Any>)>;

        template <typename... Args> Any invoke(Args &&... args) {
            static_assert(std::conjunction_v<std::is_same<Args, Any>...>);
            std::vector<Any> v{args...};
            return wrapper(std::move(v));
        }

        template <typename F, typename = std::enable_if_t<std::is_class_v<F>>> explicit Function(F &&f) {
            std::function _f = f;
            _from_lambda(std::move(_f));
        }

        template <typename T, typename R, typename... Args> explicit Function(R (T::*f)(Args...)) {
            std::function _method = [=](T &obj, Args... args) -> R { return (obj.*f)(args...); };
            _from<decltype(_method), R, T, Args...>(std::move(_method));
        }
        template <typename T, typename R, typename... Args>
        explicit Function(R (T::*f)(Args...) const) : Function((R(T::*)(Args...))f) {}
        template <typename R, typename... Args> explicit Function(const std::function<R(Args...)> &f) {
            _from<decltype(f), R, Args...>(f);
        }

        template <typename R, typename... Args> explicit Function(R (*f)(Args...)) {
            _from<decltype(f), R, Args...>(f);
        }

        [[nodiscard]] const std::vector<TypeInfo> &get_signature() const { return signature; }

      private:
        template <typename R, typename... Args> void _from_lambda(std::function<R(Args...)> &&f) {
            _from<std::function<R(Args...)>, R, Args...>(std::move(f));
        }

        template <typename T> struct get_arg_type {
            using type = std::conditional_t<
                detail::is_shared_ptr_v<std::decay_t<T>>,
                std::conditional_t<std::is_reference_v<T> && !std::is_const_v<std::remove_reference_t<T>>, T &,
                                   std::shared_ptr<detail::get_internal_t<std::remove_reference_t<T>>>>,
                T &>;
        };
        template <typename T> static typename get_arg_type<T>::type get_arg(const Any &any) {
            if constexpr (detail::is_shared_ptr_v<std::decay_t<T>>) {
                if constexpr (std::is_reference_v<T> && !std::is_const_v<std::remove_reference_t<T>>) {
                    // is not const reference
                    return any.as<T>();
                } else {
                    return any.shared_cast<detail::get_internal_t<std::remove_reference_t<T>>>();
                }
            } else {
                // not shared_ptr
                return any.as<T>();
            }
        }
#define BIG_BIG_SWITCH(BEFORE, AFTER)                                                                                  \
    if constexpr (nArgs == 0) {                                                                                        \
        BEFORE(f()) AFTER                                                                                              \
    } else if constexpr (nArgs == 1) {                                                                                 \
        BEFORE(f(get_arg<nth_element_t<0, arg_list_t>>(arg[0]))) AFTER                                                 \
    } else if constexpr (nArgs == 2) {                                                                                 \
        BEFORE(f(get_arg<nth_element_t<0, arg_list_t>>(arg[0]), get_arg<nth_element_t<1, arg_list_t>>(arg[1]))) AFTER  \
    } else if constexpr (nArgs == 3) {                                                                                 \
        BEFORE(f(get_arg<nth_element_t<0, arg_list_t>>(arg[0]), get_arg<nth_element_t<1, arg_list_t>>(arg[1]),         \
                 get_arg<nth_element_t<2, arg_list_t>>(arg[2])))                                                       \
        AFTER                                                                                                          \
    } else if constexpr (nArgs == 4) {                                                                                 \
        BEFORE(f(get_arg<nth_element_t<0, arg_list_t>>(arg[0]), get_arg<nth_element_t<1, arg_list_t>>(arg[1]),         \
                 get_arg<nth_element_t<2, arg_list_t>>(arg[2]), get_arg<nth_element_t<3, arg_list_t>>(arg[3])))        \
        AFTER                                                                                                          \
    } else if constexpr (nArgs == 5) {                                                                                 \
        BEFORE(f(get_arg<nth_element_t<0, arg_list_t>>(arg[0]), get_arg<nth_element_t<1, arg_list_t>>(arg[1]),         \
                 get_arg<nth_element_t<2, arg_list_t>>(arg[2]), get_arg<nth_element_t<3, arg_list_t>>(arg[3]),         \
                 get_arg<nth_element_t<4, arg_list_t>>(arg[4])))                                                       \
        AFTER                                                                                                          \
    } else if constexpr (nArgs == 6) {                                                                                 \
        BEFORE(f(get_arg<nth_element_t<0, arg_list_t>>(arg[0]), get_arg<nth_element_t<1, arg_list_t>>(arg[1]),         \
                 get_arg<nth_element_t<2, arg_list_t>>(arg[2]), get_arg<nth_element_t<3, arg_list_t>>(arg[3]),         \
                 get_arg<nth_element_t<4, arg_list_t>>(arg[4]), get_arg<nth_element_t<5, arg_list_t>>(arg[5])))        \
        AFTER                                                                                                          \
    } else if constexpr (nArgs == 7) {                                                                                 \
        BEFORE(f(get_arg<nth_element_t<0, arg_list_t>>(arg[0]), get_arg<nth_element_t<1, arg_list_t>>(arg[1]),         \
                 get_arg<nth_element_t<2, arg_list_t>>(arg[2]), get_arg<nth_element_t<3, arg_list_t>>(arg[3]),         \
                 get_arg<nth_element_t<4, arg_list_t>>(arg[4]), get_arg<nth_element_t<5, arg_list_t>>(arg[5]),         \
                 get_arg<nth_element_t<6, arg_list_t>>(arg[6])))                                                       \
        AFTER                                                                                                          \
    } else {                                                                                                           \
        BEFORE(f(get_arg<nth_element_t<0, arg_list_t>>(arg[0]), get_arg<nth_element_t<1, arg_list_t>>(arg[1]),         \
                 get_arg<nth_element_t<2, arg_list_t>>(arg[2]), get_arg<nth_element_t<3, arg_list_t>>(arg[3]),         \
                 get_arg<nth_element_t<4, arg_list_t>>(arg[4]), get_arg<nth_element_t<5, arg_list_t>>(arg[5]),         \
                 get_arg<nth_element_t<6, arg_list_t>>(arg[6]), get_arg<nth_element_t<7, arg_list_t>>(arg[7])))        \
        AFTER                                                                                                          \
    }
        template <typename F, typename R, typename... Args> void _from(F f) {
            signature = {type_of<Args>()...};
            wrapper = [=](std::vector<Any> arg) -> Any {
                using arg_list_t = TypeList<Args...>;
                // clang-format off
                constexpr auto nArgs = sizeof...(Args);
                if (arg.size() != nArgs) {
                    throw std::runtime_error("argument count mismatch");
                }
                static_assert(nArgs <= 8, "at most 8 args are supported");
                if constexpr (!std::is_same_v<R, void>) {
                    BIG_BIG_SWITCH(return _make_any_<R>, ;)
                } else {
                    BIG_BIG_SWITCH(;,;return Any();)
                }
                // clang-format on
            };
        }
#undef BIG_BIG_SWITCH
        FunctionWrapper wrapper;
    };

    using Attributes = std::unordered_map<std::string, std::string>;
    namespace detail {
        struct meta_instance;
        template <typename T> struct meta_instance_handle;
    } // namespace detail
    struct Property {
        friend struct detail::meta_instance;
        template <typename T> friend struct detail::meta_instance_handle;

        [[nodiscard]] const char *name() const { return _name.data(); }

        [[nodiscard]] const Attributes &attr() const { return _attr.get(); }

        Property(const char *name, const Attributes &attr) : _name(name), _attr(attr) {}

        Any get(const Any &any) {
            if (any.is_pointer()) {
                return _get(any.get_underlying());
            } else {
                return _get(any);
            }
        }

        void set(const Any &obj, const Any &value) {
            if (obj.is_pointer()) {
                _set(obj.get_underlying(), value);
            } else {
                _set(obj, value);
            }
        }

      private:
        std::function<void(const Any &, const Any &)> _set;
        std::function<Any(const Any &)> _get;
        std::function<void(const Any &, const Any &)> _set_ptr;
        std::function<Any(const Any &)> _get_ptr;
        std::string_view _name;
        std::reference_wrapper<const Attributes> _attr;
    };

    struct Method {
        friend struct detail::meta_instance;
        template <typename T> friend struct detail::meta_instance_handle;

        [[nodiscard]] const char *name() const { return _name.data(); }

        [[nodiscard]] const Attributes &attr() const { return _attr.get(); }

        Method(const char *name, const Attributes &attr) : _name(name), _attr(attr) {}

        template <typename T, typename... Args> Any invoke(T &&obj, Args &&... args) {
            return _function.invoke(make_any_ref(obj), Any(std::forward<Args>(args))...);
        }

      private:
        std::string_view _name;
        std::reference_wrapper<const Attributes> _attr;
        Function _function;
    };
    namespace detail {

        struct meta_instance {
            using GetFieldFunc = std::function<Any(const Any &)>;
            std::unordered_map<std::string, Property> properties;
            std::unordered_map<std::string, Method> methods;
            std::unordered_map<std::string, Attributes> attributes;
            std::vector<Function> constructors;
            std::vector<Function> shared_constructors;
            std::vector<std::string_view> base_classes;
            std::vector<std::string_view> derived_classes;
            TypeInfo type_info;
            std::string_view pretty_name;
        };

        template <typename U> struct meta_instance_handle {
            meta_instance_handle(meta_instance &i)
                : properties(i.properties), methods(i.methods), attributes(i.attributes), constructors(i.constructors),
                  shared_constructors(i.shared_constructors) {}

            std::unordered_map<std::string, Property> &properties;
            std::unordered_map<std::string, Method> &methods;
            std::unordered_map<std::string, Attributes> &attributes;

            std::vector<Function> &constructors;
            std::vector<Function> &shared_constructors;

            using class_type = U;
            template <typename... Args> meta_instance_handle &constructor() {
                std::function<U(Args...)> ctor = [](Args... args) { return U(std::forward<Args>(args)...); };
                std::function<std::shared_ptr<U>(Args...)> ctor_shared = [](Args... args) {
                    return std::make_shared<U>(std::forward<Args>(args)...);
                };
                constructors.emplace_back(ctor);
                shared_constructors.emplace_back(ctor_shared);
                return *this;
            }

            //            template <typename F>
            //            meta_instance_handle &method(const char *name, F&& f) {
            //                return method(name, reinterpret_cast<T (U::*)(Args...)>(f));
            //            }

            template <typename F> meta_instance_handle &method(const char *name, F &&f) {
                if (methods.count(name) > 0) {
                    throw std::runtime_error(std::string("method ") + name + " has already been defined");
                }
                auto it = attributes.find(name);
                if (it == attributes.end()) {
                    attributes.insert(std::make_pair(name, Attributes()));
                }

                auto &attr = attributes.at(name);
                methods.emplace(std::make_pair(name, Method(name, attr)));
                methods.at(name)._function = Function(f);
                return *this;
            }

            template <typename T> meta_instance_handle &property(const char *name, T U::*p) {
                if (properties.count(name) > 0) {
                    throw std::runtime_error(std::string("property ") + name + " has already been defined");
                }
                auto it = attributes.find(name);
                if (it == attributes.end()) {
                    attributes.insert(std::make_pair(name, Attributes()));
                }
                auto &attr = attributes.at(name);
                auto get = [=](const Any &any) -> Any {
                    auto &object = any.as<U>();

                    return make_any_ref(object.*p);
                };
                auto set = [=](const Any &any, const Any &value) {
                    auto &object = any.as<U>();
                    object.*p = value.as<T>();
                };
                properties.emplace(std::make_pair(name, Property(name, attr)));
                properties.at(name)._get = get;
                properties.at(name)._set = set;
                return *this;
            }

            meta_instance_handle &add_attribute(const char *name, const char *key, const char *value) {
                attributes[name][key] = value;
                return *this;
            }
        };
        struct hash_pair {
            template <class T1, class T2> size_t operator()(const std::pair<T1, T2> &p) const {
                auto hash1 = std::hash<T1>{}(p.first);
                auto hash2 = std::hash<T2>{}(p.second);
                return hash1 ^ hash2;
            }
        };
        struct AKR_EXPORT reflection_manager {
            using NameNotFoundCallback = std::function<meta_instance &(reflection_manager &, const char *)>;
            std::unordered_map<std::string_view, meta_instance> instances;
            std::unordered_map<std::string_view, std::string_view> name_map;
            std::unordered_map<std::string_view, std::string_view> inv_name_map;
            std::unordered_map<std::string_view, std::vector<Function>> functions;
            std::unordered_map<std::pair<std::string_view, std::string_view>,
                               std::function<std::optional<void *>(TypeInfo, TypeInfo, void *)>, hash_pair>
                cast_funcs;
            std::unordered_map<
                std::pair<std::string_view, std::string_view>,
                std::function<std::optional<std::shared_ptr<void>>(TypeInfo, TypeInfo, std::shared_ptr<void>)>,
                hash_pair>
                shared_cast_funcs;

            meta_instance &instance_by_name(const char *name) {
                auto it = name_map.find(name);
                if (it == name_map.end()) {
                    return name_not_found_callback(*this, name);
                } else {
                    return instances.at(it->second);
                }
            }
            meta_instance &instance_by_type(TypeInfo t) { return instances.at(t.name); }
            static reflection_manager &instance();
            NameNotFoundCallback name_not_found_callback;
        };

    } // namespace detail
    inline std::optional<void *> any_pointer_cast(TypeInfo to, TypeInfo from, void *p) {
        auto &mgr = detail::reflection_manager::instance();

        auto it = mgr.cast_funcs.find(std::make_pair(to.name, from.name));
        if (it != mgr.cast_funcs.end()) {
            return it->second(to, from, p);
        }

        return {};
    }

    inline std::optional<std::shared_ptr<void>> any_shared_pointer_cast(TypeInfo to, TypeInfo from,
                                                                        std::shared_ptr<void> p) {
        auto &mgr = detail::reflection_manager::instance();
        auto it = mgr.shared_cast_funcs.find(std::make_pair(to.name, from.name));
        if (it != mgr.shared_cast_funcs.end()) {
            return it->second(to, from, p);
        }
        return {};
    }

    namespace detail {
        template <typename T> struct register_cast_func {
            template <typename To, typename From> static void do_it1() {
                auto &mgr = detail::reflection_manager::instance();
                std::function<std::optional<void *>(TypeInfo, TypeInfo, void *)> f =
                    [](TypeInfo to, TypeInfo from, void *p) -> std::optional<void *> {
                    if (to == type_of<To>() && from == type_of<From>()) {
                        auto q = dynamic_cast<To *>(reinterpret_cast<From *>(p));
                        return q;
                    }
                    return {};
                };

                mgr.cast_funcs.emplace(std::make_pair(type_of<To>().name, type_of<From>().name), f);
            }

            template <typename To, typename From> static void do_it_shared1() {
                auto &mgr = detail::reflection_manager::instance();
                std::function<std::optional<std::shared_ptr<void>>(TypeInfo, TypeInfo, std::shared_ptr<void>)> f =
                    [](TypeInfo to, TypeInfo from, std::shared_ptr<void> p) -> std::optional<std::shared_ptr<void>> {
                    if (to == type_of<To>() && from == type_of<From>()) {
                        auto q = std::dynamic_pointer_cast<To>(std::reinterpret_pointer_cast<From>(p));
                        return q;
                    } else {
                        return {};
                    }
                };

                mgr.shared_cast_funcs.emplace(std::make_pair(type_of<To>().name, type_of<From>().name), f);
            }
            template <typename U, typename... Rest> static void do_it_shared() {
                do_it_shared1<T, U>();
                do_it_shared1<U, T>();

                if constexpr (sizeof...(Rest) > 0) {
                    do_it<Rest...>();
                }
            }
            template <typename U, typename... Rest> static void do_it() {
                do_it1<T, U>();
                do_it1<U, T>();
                auto &mgr = detail::reflection_manager::instance();
                if (mgr.instances.count(type_of<U>().name) == 0) {
                    mgr.instances[type_of<U>().name] = {};
                }
                {
                    auto &derived = mgr.instances.at(type_of<T>().name);
                    auto &parent = mgr.instances.at(type_of<U>().name);
                    derived.base_classes.emplace_back(type_of<U>().name);
                    parent.derived_classes.emplace_back(type_of<T>().name);
                }
                if constexpr (sizeof...(Rest) > 0) {
                    do_it_shared<Rest...>();
                }
            }
        };
    } // namespace detail
    template <typename T, typename... Base> detail::meta_instance_handle<T> class_(const char *name = nullptr) {
        auto type = type_of<T>();
        auto &mgr = detail::reflection_manager::instance();
        if (mgr.instances.find(type.name) == mgr.instances.end()) {
            mgr.instances[type.name] = detail::meta_instance();
            if (name) {
                mgr.name_map.emplace(name, type_of<T>().name);
                mgr.inv_name_map.emplace(type_of<T>().name, name);
            }
        }
        auto & instance =  mgr.instances.at(type.name);
        instance.type_info = type;
        if(name)
           instance.pretty_name = name;
        else if(instance.pretty_name.empty() || instance.pretty_name == "" )
            instance.pretty_name = type.name;
        if constexpr (sizeof...(Base) > 0) {
            detail::register_cast_func<T>::template do_it<Base...>();
            detail::register_cast_func<T>::template do_it_shared<Base...>();
        }
        auto c = detail::meta_instance_handle<T>(instance);
        if constexpr (std::is_default_constructible_v<T>) {
            c.template constructor<>();
        }
        return c;
    }

    template <typename F> void function(const char *name, F &&f) {
        auto &mgr = detail::reflection_manager::instance();
        mgr.functions[name].emplace_back(f);
    }

    template <typename... Args> inline Any dynamic_invoke(const char *name, Args &&... args) {
        auto &mgr = detail::reflection_manager::instance();
        auto &funcs = mgr.functions.at(name);
        for (auto &func : funcs) {
            try {
                return func.invoke(Any(std::forward<Args>(args))...);
            } catch (std::runtime_error &e) {
            }
        }
        throw std::runtime_error(std::string("no matching function when calling ") + name);
    }

    template <typename... Args> inline std::optional<Any> dynamic_invoke_noexcept(const char *name, Args &&... args) {
        auto &mgr = detail::reflection_manager::instance();
        auto &funcs = mgr.functions.at(name);
        for (auto &func : funcs) {
            try {
                return func.invoke(Any(std::forward<Args>(args))...);
            } catch (std::runtime_error &e) {
            }
        }
        return {};
    }

    struct Type {
        template <typename T> struct _tag {};

        template <typename T> static const Type &get() {
            static Type _this_type(_tag<T>{});
            return _this_type;
        }

        static Type get_by_name(const char *name) {
            Type _this_type(detail::reflection_manager::instance().instance_by_name(name));
            return _this_type;
        }

        static Type get(const Any &any) { return Type(get_by_typeid(any.type.name)); }

        template <typename T> static Type get_by_typeid(T &&v) {
            Type _this_type(type_of(v).name);
            return _this_type;
        }

        [[nodiscard]] bool has_property(const char *name) const {
            return _get().properties.find(name) != _get().properties.end();
        }

        [[nodiscard]] bool has_method(const char *name) const {
            return _get().methods.find(name) != _get().methods.end();
        }

        [[nodiscard]] Property get_property(const char *name) const { return _get().properties.at(name); }

        [[nodiscard]] Method &get_method(const char *name) const { return _get().methods.at(name); }

        [[nodiscard]] std::vector<Property> get_properties() const {
            std::vector<Property> v;
            for (auto &field : _get().properties) {
                v.emplace_back(field.second);
            }
            return v;
        }

        template <typename... Args>[[nodiscard]] Any create(Args &&... args) const {
            for (auto &ctor : _get().constructors) {
                try {
                    return ctor.invoke(Any(std::forward<Args>(args))...);
                } catch (std::runtime_error &) {
                }
            }
            throw std::runtime_error("no matching constructor");
        }

        template <typename... Args>[[nodiscard]] Any create_shared(Args &&... args) const {
            for (auto &ctor : _get().shared_constructors) {
                try {
                    return ctor.invoke(Any(std::forward<Args>(args))...);
                } catch (std::runtime_error &) {
                }
            }
            throw std::runtime_error("no matching constructor");
        }

        Type(TypeInfo typeInfo) : Type(typeInfo.name) {}

        template<typename F>
        void foreach_base(F && f){
            auto & instance = _get();
            for(auto & base : instance.base_classes){
                auto base_type = Type(base);
                f(base_type);
            }
        }
        template<typename F>
        void foreach_derived(F && f){
            auto & instance = _get();
            for(auto & base : instance.derived_classes){
                auto base_type = Type(base);
                f(base_type);
            }
        }
        [[nodiscard]] std::string_view pretty_name()const{
            auto & instance = _get();
            return instance.pretty_name.data();
        }
      private:
        std::function<detail::meta_instance &(void)> _get;
        Type(detail::meta_instance &instance) {
            _get = [&]() -> detail::meta_instance & { return instance; };
        }
        Type(std::string_view type) {
            _get = [=]() -> detail::meta_instance & {
                auto &mgr = detail::reflection_manager::instance();
                // possible thread-safety issue
                auto &instance = mgr.instances[type];
                return instance;
            };
        }

        template <typename T> explicit Type(_tag<T>) : Type(type_of<T>().name) {}
    };

} // namespace akari