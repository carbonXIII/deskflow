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

#include "platform/XdpConversions.h"

#include "gtest/gtest.h"

namespace deskflow::conv {
enum class TestEnum : int
{
};

struct TestView
{
  using Like = std::tuple<unsigned, std::vector<unsigned>>;
  unsigned x;
  std::vector<unsigned> y;
};

struct F
{
  using Like = std::tuple<int>;
  int deg;
};

struct C
{
  using Like = std::tuple<int>;
  int deg;
};

struct UnitConv : AdaptorBase<F, C>
{
  int temp;
  constexpr UnitConv &operator<<(F const &f)
  {
    temp = {((f.deg - 32) * 5 / 9)};
    return *this;
  }

  friend constexpr void complete(UnitConv &self)
  {
    self << C{self.temp};
  }
};

template <typename T>
concept ValidClosure = Closure<decltype(closure_for<T>)>;

using SupportedTypes = ::testing::Types<
    unsigned, TestEnum, std::optional<unsigned>, std::vector<unsigned>, std::tuple<std::string, unsigned>,
    std::array<unsigned, 3>, TestView>;

template <typename T> struct XdpConversionsTypedTests : testing::Test
{};

TYPED_TEST_SUITE(XdpConversionsTypedTests, SupportedTypes);

TYPED_TEST(XdpConversionsTypedTests, ValidForAll)
{
  ASSERT_TRUE(ValidClosure<TypeParam>);
}

TEST(XdpConversionsTests, SpecialConcepts)
{
  static_assert(optional_like<std::optional<unsigned>>);
  static_assert(view_like<TestView>);
}

TEST(XdpConversionsTests, BasicConversion)
{
  static constexpr auto res = []() {
    auto conv = UnitConv{};
    conv << F{75};
    return get(conv).value_or(C{-1}).deg;
  }();
  ASSERT_EQ(res, 23);
}

TEST(XdpConversionsTests, TupleConstruction)
{
  static constexpr auto expected = std::array<int, 2>{6, 7};
  static constexpr auto res = []() -> std::array<int, 2> {
    if (auto val = get(closure_for<std::tuple<F, C>>() << expected)) {
      auto [f, c] = *val;
      return {f.deg, c.deg};
    } else {
      return {-1, -1};
    }
  }();
  ASSERT_EQ(res, expected);
}
} // namespace deskflow::conv
