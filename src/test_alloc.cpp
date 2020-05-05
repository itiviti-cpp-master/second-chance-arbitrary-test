#include "pool.h"

#include <gtest/gtest.h>

#include <vector>

namespace {

template <std::size_t Size>
struct Dummy
{
    unsigned char data[Size];

    Dummy()
    {
        for (std::size_t i = 0; i < Size; ++i) {
            data[i] = 0x5a;
        }
    }

    ~Dummy()
    {
        for (std::size_t i = 0; i < Size; ++i) {
            data[i] = 0xa5;
        }
    }

    bool check() const
    {
        for (std::size_t i = 0; i < Size; ++i) {
            if (data[i] != 0x5a) {
                return false;
            }
        }
        return true;
    }
};

struct Complex
{
    int a;
    char & b;
    const double c;

    Complex(const int a_, char & b_, const double c_)
        : a(a_), b(b_), c(c_)
    {}

    ~Complex()
    {
        a = -1;
    }
};

template <std::size_t Size, std::size_t Count>
struct Params
{
    static constexpr std::size_t size = Size;
    static constexpr std::size_t count = Count;
};

template <class P>
struct AllocatorTest : ::testing::Test
{
    static constexpr std::size_t pool_size = P::size * P::count;

    PoolAllocator alloc;

    AllocatorTest()
        : alloc(pool_size)
    {}

    using D = Dummy<P::size>;

    D * create_dummy()
    {
        return new (alloc.allocate(sizeof(D))) D;
    }

    void destroy_dummy(const D * ptr)
    {
        if (ptr != nullptr) {
            ptr->~D();
        }
        alloc.deallocate(ptr);
    }

    Complex * create_complex(const int a, char & b, const double c)
    {
        return new (alloc.allocate(sizeof(Complex))) Complex(a, b, c);
    }

    void destroy_complex(const Complex * ptr)
    {
        if (ptr != nullptr) {
            ptr->~Complex();
        }
        alloc.deallocate(ptr);
    }
};

using TestedTypes = ::testing::Types<Params<1, 1>, Params<1, 24>, Params<3, 1>, Params<7, 4>, Params<7, 15>, Params<10, 10>, Params<256, 1>, Params<256, 256>>;
TYPED_TEST_SUITE(AllocatorTest, TestedTypes);

} // anonymous namespace

TYPED_TEST(AllocatorTest, single_dummy)
{
    auto * ptr = this->create_dummy();
    ptr->data[0] = 112;
    this->destroy_dummy(ptr);
}

TYPED_TEST(AllocatorTest, single_complex)
{
    char x = '@';
    if (this->pool_size >= sizeof(Complex)) {
        const auto * ptr = this->create_complex(-511, x, 0.05);
        EXPECT_EQ(-511, ptr->a);
        EXPECT_EQ('@', ptr->b);
        EXPECT_EQ(0.05, ptr->c);
        this->destroy_complex(ptr);
    }
    else {
        EXPECT_THROW(this->create_complex(0, x, 0.01), std::bad_alloc);
    }
}

TYPED_TEST(AllocatorTest, full_dummy)
{
    using Ptr = decltype(this->create_dummy());
    std::vector<Ptr> ptrs;
    for (std::size_t i = 0; i < TypeParam::count; ++i) {
        auto * ptr = this->create_dummy();
        ptr->data[0] = 199;
        ptrs.push_back(ptr);
    }
    EXPECT_THROW(this->create_dummy(), std::bad_alloc);
    EXPECT_THROW(this->create_dummy(), std::bad_alloc);
    for (const auto ptr : ptrs) {
        EXPECT_EQ(199, ptr->data[0]);
        this->destroy_dummy(ptr);
    }
    EXPECT_NO_THROW(this->destroy_dummy(this->create_dummy()));
}

TYPED_TEST(AllocatorTest, full_complex)
{
    const std::size_t complex_count = this->pool_size / sizeof(Complex);
    std::vector<Complex *> ptrs;
    char x = 'X';
    int n = -11;
    const double d = 1.11e-3;
    for (std::size_t i = 0; i < complex_count; ++i, --n) {
        auto * ptr = this->create_complex(n, x, d);
        EXPECT_EQ(n, ptr->a);
        EXPECT_EQ(x, ptr->b);
        EXPECT_EQ(d, ptr->c);
        ptrs.push_back(ptr);
    }
    if (this->pool_size >= sizeof(Complex)) {
        EXPECT_THROW(this->create_complex(0, x, 0.01), std::bad_alloc);
    }
    n = -11;
    for (const auto ptr : ptrs) {
        EXPECT_EQ(n, ptr->a);
        EXPECT_EQ(x, ptr->b);
        EXPECT_EQ(d, ptr->c);
        --n;
        this->destroy_complex(ptr);
    }
    if (this->pool_size >= sizeof(Complex)) {
        EXPECT_NO_THROW(this->destroy_complex(this->create_complex(0, x, 0.01)));
    }
}

TYPED_TEST(AllocatorTest, full_mixed)
{
    using Ptr = decltype(this->create_dummy());
    std::vector<Ptr> d_ptrs;
    std::vector<Complex *> c_ptrs;
    char x = '7';
    const double d = 100.99;
    const int n = -113;
    const unsigned char u = 0x1F;
    std::size_t available = this->pool_size;
    while (available >= TypeParam::size || available >= sizeof(Complex)) {
        if (available >= sizeof(Complex)) {
            c_ptrs.push_back(this->create_complex(n, x, d));
            available -= sizeof(Complex);
        }
        if (available >= TypeParam::size) {
            d_ptrs.push_back(this->create_dummy());
            d_ptrs.back()->data[0] = u;
            available -= TypeParam::size;
        }
    }
    EXPECT_TRUE(available < TypeParam::size && available < sizeof(Complex));
    EXPECT_THROW(this->create_dummy(), std::bad_alloc);
    if (this->pool_size >= sizeof(Complex)) {
        EXPECT_THROW(this->create_complex(0, x, 0.01), std::bad_alloc);
    }
    for (const auto ptr : c_ptrs) {
        EXPECT_EQ(n, ptr->a);
        EXPECT_EQ(x, ptr->b);
        EXPECT_EQ(d, ptr->c);
        this->destroy_complex(ptr);
    }
    for (const auto ptr : d_ptrs) {
        EXPECT_EQ(u, ptr->data[0]);
        this->destroy_dummy(ptr);
    }
}

TYPED_TEST(AllocatorTest, dummy_fragmentation)
{
    using Ptr = decltype(this->create_dummy());
    std::vector<Ptr> d_ptrs;
    for (std::size_t i = 0; i < TypeParam::count; ++i) {
        d_ptrs.push_back(this->create_dummy());
    }

    std::size_t available = 0;
    for (std::size_t i = 0; i < TypeParam::count; i += 2) {
        this->destroy_dummy(d_ptrs[i]);
        d_ptrs[i] = nullptr;
        available += TypeParam::size;
    }

    char x = ' ';
    const double d = 0xF.Fp10;
    int n = 0;
    std::vector<Complex *> c_ptrs;
    while (available >= 2 * sizeof(Complex)) {
        EXPECT_NO_THROW(c_ptrs.push_back(this->create_complex(n, x, d)));
        ++n;
        available -= sizeof(Complex);
    }

    for (const auto ptr : d_ptrs) {
        if (ptr != nullptr) {
            EXPECT_TRUE(ptr->check());
        }
        this->destroy_dummy(ptr);
    }
    n = 0;
    for (const auto ptr : c_ptrs) {
        EXPECT_EQ(n, ptr->a);
        EXPECT_EQ(x, ptr->b);
        EXPECT_EQ(d, ptr->c);
        this->destroy_complex(ptr);
        ++n;
    }
}

TYPED_TEST(AllocatorTest, complex_fragmentation)
{
    const std::size_t complex_num = this->pool_size / sizeof(Complex);
    char x = ' ';
    const double d = 0xF.Fp10;
    int n = 0;
    std::vector<Complex *> c_ptrs;
    for (std::size_t i = 0; i < complex_num; ++i) {
        c_ptrs.push_back(this->create_complex(n, x, d));
        ++n;
    }

    std::size_t available = 0;
    for (std::size_t i = 0; i < complex_num; i += 2) {
        this->destroy_complex(c_ptrs[i]);
        c_ptrs[i] = nullptr;
        available += sizeof(Complex);
    }

    using Ptr = decltype(this->create_dummy());
    std::vector<Ptr> d_ptrs;
    while (available >= 2 * TypeParam::size) {
        EXPECT_NO_THROW(d_ptrs.push_back(this->create_dummy()));
        available -= TypeParam::size;
    }

    n = 0;
    for (const auto ptr : c_ptrs) {
        if (ptr != nullptr) {
            EXPECT_EQ(n, ptr->a);
            EXPECT_EQ(x, ptr->b);
            EXPECT_EQ(d, ptr->c);
        }
        this->destroy_complex(ptr);
        ++n;
    }
    for (const auto ptr : d_ptrs) {
        EXPECT_TRUE(ptr->check());
        this->destroy_dummy(ptr);
    }
}
