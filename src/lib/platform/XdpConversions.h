/*
 * Deskflow -- mouse and keyboard sharing utility
 * Copyright (C) 2025 Jordan Richards
 *
 * This package is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * found in the file LICENSE that should have accompanied this file.
 *
 * This package is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <QDBusArgument>
#include <QDBusObjectPath>
#include <QList>
#include <QVariant>
#include <ranges>
#include <source_location>

#include <base/Log.h>
#include <base/String.h>

namespace deskflow {
#define FORWARD(x) std::forward<decltype(x)>(x)

template <std::size_t N> struct in_place_str
{
  char data[N];
  consteval in_place_str(const char (&str)[N])
  {
    std::copy_n(str, N, data);
  }
};
template <int N> in_place_str(const char (&str)[N]) -> in_place_str<N>;

template <in_place_str... keys> struct OptionFilter
{
  template <typename Res> static constexpr Res map_element(auto it, auto end)
  {
    return it == end ? Res{} : Res{*it};
  }

  QVariantList operator()(QVariantMap const &map) const
  {
    auto end = map.end();
    return QVariantList{map_element<QVariant>(map.find(keys.data), end)...};
  }
};

template <in_place_str... keys> static constexpr auto option_filter = OptionFilter<keys...>{};
template <typename T> static constexpr auto filter_for_v = false;

namespace conv {
using Variant = QVariant;
using Tuple = QVariantList;
template <typename T> using Array = QList<T>;
using Map = QVariantMap;
using Argument = QDBusArgument;

template <typename T> static constexpr auto closure_for = false;

struct Error
{
  static constexpr Error log(std::source_location const &loc = std::source_location::current(), auto... args)
  {
    if constexpr (!std::is_constant_evaluated()) {
      auto msg = deskflow::string::sprintf(args...);
      LOG_WARN("XdpConversions (%s): (%s)", loc.function_name(), msg.c_str());
    }
    return {};
  }
};
#define ERROR_LOG(...) Error::log(std::source_location::current(), __VA_ARGS__)

/// Wrapper indicating an argument is a child value for aggregate adaptors
template <typename T> struct Value
{
  T t;
  constexpr T const &operator*() const
  {
    return t;
  }
};

template <> struct Value<QDBusVariant>
{
  QDBusVariant var;
  Variant operator*() const
  {
    return var.variant();
  }
};

template <> struct Value<QDBusArgument>
{
  QDBusArgument var;
  Variant operator*() const
  {
    return var.asVariant();
  }
};

template <typename T> Value(T &&) -> Value<std::decay_t<T>>;

// clang-format: off
template <typename Self, typename T = typename Self::input_type>
concept Sink = requires { typename Self::input_type; } && std::same_as<typename Self::input_type, T> &&
               requires(Self &self, T const &t) {
                 { self << t } -> std::same_as<Self &>;
                 { self << Error{} } -> std::same_as<Self &>;
               };

template <typename T> struct CanonSink
{
  using input_type = T;
  CanonSink &operator<<(T const &);
  CanonSink &operator<<(Error const &)
    requires(!std::same_as<T, Error>);
};
static_assert(Sink<CanonSink<unsigned>>);

template <typename Self, typename T = Self::output_type>
concept Source = requires { typename Self::output_type; } && std::same_as<typename Self::output_type, T> &&
                 requires(Self &source, CanonSink<typename Self::output_type> &sink) {
                   { sink << source } -> std::same_as<CanonSink<typename Self::output_type> &>;
                 };

template <typename T, typename U>
concept SinkFor = Sink<T> && Source<U> && std::same_as<typename U::output_type, typename T::input_type>;

template <typename Self>
concept Adaptor = requires {
  typename Self::input_type;
  typename Self::output_type;
} && Sink<Self> && Source<Self>;

template <typename T>
concept Closure = Sink<decltype(T{}())> && Source<decltype(T{}())>;
// clang-format: off

template <typename Input, typename Output, bool _optional = false> struct AdaptorBase
{
  static constexpr bool generic_adaptor_tag = {};
  static constexpr bool optional = _optional;
  using input_type = Input;
  using output_type = Output;

  std::optional<output_type> result;
  bool error = false;

  friend constexpr void complete(AdaptorBase &)
  {}
};

template <typename Self>
concept GenericAdaptor = requires { std::decay_t<Self>::generic_adaptor_tag; };

template <GenericAdaptor Self> constexpr std::optional<typename Self::output_type> get(Self self)
{
  complete(self);
  if (self.error || (!self.result && !Self::optional))
    return std::nullopt;
  if (!self.result && Self::optional) {
    return typename Self::output_type{};
  }
  return self.result;
}

constexpr auto &operator<<(GenericAdaptor auto &&self, Error const &e)
{
  self.error = true;
  self.result.reset();
  return self;
}

template <GenericAdaptor Self_, typename T>
  requires std::same_as<std::decay_t<T>, typename std::decay_t<Self_>::output_type>
constexpr auto &operator<<(Self_ &&self, T &&t)
{
  using Self = std::decay_t<Self_>;
  if (self.error)
    return self;
  if (!self.result) {
    self.result.emplace(FORWARD(t));
  } else {
    self.error = true;
    self.result.reset();
  }

  return self;
}

template <typename Sink_, GenericAdaptor Self> constexpr Sink_ &operator<<(Sink_ &sink, Self &&self)
{
  if (auto res = get(self)) {
    return sink << std::move(res).value();
  } else {
    return sink << Error{};
  }
}

template <typename Target, bool argument_supported = true> struct VariantVisitor
{
  static constexpr bool variant_visitor_tag = {};
  using variant_type = Target;
  static constexpr bool is_argument_supported = argument_supported;
};

template <typename Self>
concept GenericVariantVisitor = requires { std::decay_t<Self>::variant_visitor_tag; };

template <GenericVariantVisitor Self_, typename T>
  requires std::same_as<T, Variant>
constexpr Self_ &operator<<(Self_ &&self, T const &var)
{
  using Self = std::decay_t<Self_>;
  if (var.isNull())
    return self;
  if constexpr (Self::is_argument_supported) {
    if (var.template canConvert<Argument>())
      return self << var.template value<Argument>();
  }
  if constexpr (std::same_as<typename Self::variant_type, std::string>) {
    if (var.template canConvert<QDBusObjectPath>())
      return self << var.template value<QDBusObjectPath>().path().toStdString();
    if (var.template canConvert<QString>())
      return self << var.template value<QString>().toStdString();
  }
  if (var.template canConvert<typename Self::variant_type>())
    return self << var.template value<typename Self::variant_type>();
  return self << ERROR_LOG("Unexpected variant type (%s)", var.typeName());
}

template <auto... Keys> static constexpr bool pack_switch(auto const &key, auto &&func)
{
  bool ret = true;
  auto call = [&](auto &&arg) {
    if constexpr (std::is_same_v<void, std::invoke_result_t<decltype(func), decltype(arg)>>) {
      std::invoke(func, FORWARD(arg));
    } else {
      ret = ret && std::invoke(func, FORWARD(arg));
    }
  };
  return std::invoke(
             [&]<std::size_t... Is>(std::index_sequence<Is...>) {
               return ((key == Keys && (call(std::index_sequence<Is>{}), true)) || ...);
             },
             std::make_index_sequence<sizeof...(Keys)>{}
         ) &&
         ret;
}

template <typename... T> static constexpr void exchange(auto const &left, auto &right)
{
  std::apply(
      [&](T... t) {
        ((left >> t) >> ...);
        ((right << Value{t}) << ...);
      },
      std::tuple<T...>{}
  );
}

template <typename T, bool optional = false, typename Container = std::conditional_t<optional, std::optional<T>, T>>
struct BasicAdaptor : AdaptorBase<Variant, Container, optional>, VariantVisitor<T, false>
{
  BasicAdaptor &operator<<(T &&t)
    requires(!std::same_as<T, Container>)
  {
    return (*this) << Container{FORWARD(t)};
  }
};

template <typename T> struct OptionalAdaptor : AdaptorBase<Variant, T, true>
{
  OptionalAdaptor &operator<<(T::value_type &&t)
  {
    (*this) << T{t};
  }
  OptionalAdaptor &operator<<(Variant const &var)
  {
    if (var.isNull())
      return *this;
    return (*this) << get(closure_for<typename T::value_type>() << var);
  }
};

template <typename T, typename Input = std::underlying_type_t<T>> struct EnumAdaptor : AdaptorBase<Input, T>
{
  EnumAdaptor &operator<<(Input in)
  {
    return (*this) << T{in};
  }
};

template <in_place_str... keys>
struct MapAdaptor : AdaptorBase<Variant, std::array<Variant, sizeof...(keys)>>, VariantVisitor<Map>
{
  using output_type = std::array<Variant, sizeof...(keys)>;

  QString key;
  output_type v;

  MapAdaptor &operator<<(Map const &map)
  {
    for (auto it = map.begin(); it != map.end(); ++it)
      (*this) << Value{it.key()} << Value{*it};
    return *this;
  }

  MapAdaptor &operator<<(Argument const &arg)
  {
    if (arg.currentType() != Argument::MapType)
      return (*this) << ERROR_LOG("Expected map argument type, got %d", (int)arg.currentType());

    for (arg.beginMap(); !arg.atEnd();) {
      arg.beginMapEntry();
      exchange<QString, QDBusVariant>(arg, *this);
      arg.endMapEntry();
    }
    arg.endMap();

    return *this;
  }

  template <typename V> MapAdaptor &operator<<(Value<V> const &key_or_value)
  {
    if (key.size() > 0) {
      pack_switch<keys...>(key, [&]<std::size_t I>(std::index_sequence<I>) { v[I] = *key_or_value; });
      key.clear();
    } else {
      key = key_or_value;
    }
    return *this;
  }

  friend constexpr void complete(MapAdaptor &self)
  {
    if (self.key.size() != 0) {
      self << ERROR_LOG("Map incomplete (extra key)");
    } else {
      self << std::move(self.v);
    }
  }
};

template <typename T, typename Tv = std::ranges::range_value_t<T>>
struct ArrayAdaptor : AdaptorBase<Variant, T>, VariantVisitor<Tuple>
{
  using output_type = T;
  T v;

  ArrayAdaptor &operator<<(Argument const &arg)
  {
    if (arg.currentType() != Argument::ArrayType)
      return (*this) << ERROR_LOG("Expected array argument type, got %d", (int)arg.currentType());

    for (arg.beginArray(); !arg.atEnd();) {
      *this << Value{arg};
    }
    arg.endArray();

    return *this;
  }

  ArrayAdaptor &operator<<(std::ranges::range auto const &list)
  {
    for (auto const &v : list)
      (*this) << Value{v};
    return *this;
  }

  template <typename V> ArrayAdaptor &operator<<(Value<V> const &value)
  {
    if (auto res = get(closure_for<Tv>() << *value)) {
      v.emplace_back(*res);
    } else {
      (*this) << ERROR_LOG("Child invalid");
    }

    return *this;
  }

  friend constexpr void complete(ArrayAdaptor &self)
  {
    self << std::move(self.v);
  }
};

template <typename Tup, std::size_t... Is> struct TupleAdaptor : AdaptorBase<Variant, Tup>, VariantVisitor<Tuple>
{
  Tup v;
  std::size_t idx = 0;

  template <typename T>
  constexpr TupleAdaptor &operator<<(T &&arg)
    requires(sizeof...(Is) == 1) && std::same_as<std::decay_t<T>, std::tuple_element_t<0, Tup>>
  {
    return *this << Value{FORWARD(arg)};
  }

  TupleAdaptor &operator<<(Argument const &arg)
  {
    if (arg.currentType() != Argument::StructureType)
      return (*this) << ERROR_LOG("Expected structure argument type, got %d", (int)arg.currentType());

    arg.beginStructure();
    for (std::size_t i = 0; i < sizeof...(Is); ++i) {
      *this << Value{arg};
    }
    arg.endStructure();

    return *this;
  }

  constexpr TupleAdaptor &operator<<(std::ranges::range auto const &list)
  {
    for (auto const &v : list)
      (*this) << Value{v};
    return *this;
  }

  template <typename V> constexpr TupleAdaptor &operator<<(Value<V> const &value)
  {
    auto res = pack_switch<Is...>(idx++, [&]<std::size_t I>(std::index_sequence<I>) -> bool {
      using Tv = std::tuple_element_t<I, Tup>;

      if (auto res = get(closure_for<Tv>() << *value)) {
        std::get<I>(v) = std::move(res).value();
        return true;
      } else {
        return false;
      }
    });

    if (!res)
      return (*this) << ERROR_LOG("Tuple element %d, out of bounds or invalid", idx - 1);
    return *this;
  }

  constexpr friend void complete(TupleAdaptor &self)
  {
    if (self.idx < sizeof...(Is)) {
      self << ERROR_LOG("Tuple incomplete");
    } else {
      self << std::move(self.v);
    }
  }
};

template <typename T> struct ViewAdaptor : AdaptorBase<typename T::Like, T>
{
  using Like = T::Like;

  constexpr ViewAdaptor &operator<<(Like const &like)
  {
    return (*this) << std::make_from_tuple<T>(like);
  }
};

template <Adaptor L, SinkFor<L> R> struct AdaptorChain : AdaptorBase<typename L::input_type, typename R::output_type>
{
  using input_type = L::input_type;
  using output_type = R::output_type;

  L lhs;
  R rhs;

  // L.Sink<input_type>

  template <typename T>
  constexpr AdaptorChain &operator<<(T const &value)
    requires(!std::same_as<T, Error> && !std::same_as<T, output_type>)
  {
    lhs << value;
    return *this;
  };

  friend constexpr void complete(AdaptorChain &self)
  {
    self.rhs << self.lhs;
    self << self.rhs;
  }
};
template <typename T> static constexpr Closure auto make_closure = []() -> T { return {}; };

template <Closure Lhs, Closure Rhs> constexpr Closure auto operator|(Lhs const &, Rhs const &)
{
  return []() { return AdaptorChain{.lhs = Lhs{}(), .rhs = Rhs{}()}; };
}

template <typename T>
static constexpr auto map_closure = std::
    invoke([]<in_place_str... keys>(OptionFilter<keys...> const &) { return make_closure<MapAdaptor<keys...>>; }, filter_for_v<T>);

template <typename T> static constexpr auto view_closure = make_closure<ViewAdaptor<T>>;

template <typename T>
static constexpr auto tuple_closure = std::invoke(
    []<std::size_t... Is>(std::index_sequence<Is...>) { return make_closure<TupleAdaptor<T, Is...>>; },
    std::make_index_sequence<std::tuple_size_v<T>>{}
);

template <typename T> static constexpr auto array_closure = make_closure<ArrayAdaptor<T>>;

template <typename T, bool optional = false, typename Container = T>
static constexpr auto basic_closure = make_closure<BasicAdaptor<T, optional, Container>>;

template <typename T> static constexpr auto enum_closure = make_closure<EnumAdaptor<T>>;

template <typename T> static constexpr auto optional_closure = make_closure<OptionalAdaptor<T>>;

template <typename T>
concept map_like = requires { typename T::Like; } && !std::same_as<std::decay_t<decltype(filter_for_v<T>)>, bool>;
template <map_like T>
static constexpr auto closure_for<T> = map_closure<T> | closure_for<typename T::Like> | view_closure<T>;

template <typename T>
concept view_like = requires { typename T::Like; } && !map_like<T>;
template <view_like T> static constexpr auto closure_for<T> = closure_for<typename T::Like> | view_closure<T>;

template <typename T>
concept tuple_like = requires { std::tuple_size<T>::value; };
template <tuple_like T> static constexpr auto closure_for<T> = tuple_closure<T>;

template <typename T>
concept array_like = std::ranges::range<T> && !tuple_like<T> && !std::same_as<std::ranges::range_value_t<T>, char> &&
                     !std::same_as<std::ranges::range_value_t<T>, QChar>;
template <array_like T> static constexpr auto closure_for<T> = array_closure<T>;

template <typename T>
concept optional_like = std::is_default_constructible_v<T> && requires(T &t) {
  { t.has_value() } -> std::convertible_to<bool>;
  { t.value() } -> std::convertible_to<typename T::value_type>;
  { t = std::declval<typename T::value_type>() };
};
template <optional_like T> static constexpr auto closure_for<T> = optional_closure<T>;

template <typename T>
concept enum_like = std::is_enum_v<T>;
template <enum_like T>
static constexpr auto closure_for<T> = basic_closure<std::underlying_type_t<T>> | enum_closure<T>;

template <typename T>
concept basic = !map_like<T> && !tuple_like<T> && !array_like<T> && !view_like<T> && !optional_like<T> && !enum_like<T>;
template <basic T> static constexpr auto closure_for<T> = basic_closure<T>;

#undef ERROR_LOG
} // namespace conv

template <typename T, typename U> static std::optional<T> from_qvariant(U const &u) noexcept
{
  return get(conv::closure_for<T>() << u);
}

template <conv::tuple_like T> static conv::Argument &operator<<(conv::Argument &arg, T const &x)
{
  static constexpr auto N = std::tuple_size_v<T>;
  [&]<std::size_t... I>(std::index_sequence<I...>) {
    arg.beginStructure();
    ((arg << std::get<I>(x), false) || ...);
    arg.endStructure();
  }(std::make_index_sequence<N>{});
  return arg;
}

template <conv::array_like T> static conv::Argument &operator<<(conv::Argument &arg, T const &x)
{
  arg.beginArray();
  for (auto &v : x)
    arg << v;
  arg.endArray();
  return arg;
}

static conv::Variant to_qdbus(auto const &x)
{
  conv::Argument ret;
  ret << x;
  return QVariant::fromValue(ret);
}

template <typename... Args> [[nodiscard]] static bool qvariant_apply_ex(auto func, auto const &args)
{
  if (auto unpacked = from_qvariant<std::tuple<std::decay_t<Args>...>>(args)) {
    const auto v = *unpacked;
    std::apply(func, FORWARD(v));
    return true;
  }

  return false;
}

template <typename... Args> [[nodiscard]] static bool qvariant_apply(void (*func)(Args...), auto const &args)
{
  return qvariant_apply_ex<Args...>(func, args);
}

template <typename Self, typename... Args>
[[nodiscard]] static bool qvariant_apply(Self *self, void (Self::*func)(Args...) const, auto const &args)
{
  return qvariant_apply_ex<Args...>([self, func](auto &&...args) { std::invoke(func, self, FORWARD(args)...); }, args);
}

template <typename Self, typename... Args>
[[nodiscard]] static bool qvariant_apply(Self *self, void (Self::*func)(Args...), auto const &args)
{
  return qvariant_apply_ex<Args...>([self, func](auto &&...args) { std::invoke(func, self, FORWARD(args)...); }, args);
}

[[nodiscard]] static bool qvariant_apply(auto func, auto &&args)
{
  return qvariant_apply(&func, &decltype(func)::operator(), FORWARD(args));
}

#undef FORWARD
} // namespace deskflow
