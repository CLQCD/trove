/*
Copyright (c) 2013, NVIDIA Corporation
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the <organization> nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#pragma once
#include <trove/utility.h>
#include <trove/rotate.h>
#include <trove/static_mod_inverse.h>
#include <trove/static_gcd.h>
#include <trove/warp.h>

namespace trove {
namespace detail {

struct odd{};
struct power_of_two{};
struct composite{};

template<int m, bool ispo2=is_power_of_two<m>::value, bool isodd=is_odd<m>::value>
struct tx_algorithm {
    typedef composite type;
};

template<int m>
struct tx_algorithm<m, true, false> {
    typedef power_of_two type;
};

template<int m>
struct tx_algorithm<m, false, true> {
    typedef odd type;
};

template<typename Tile, int m, typename Schema=typename tx_algorithm<m>::type>
struct c2r_offset_constants{};

template<typename Tile, int m>
struct c2r_offset_constants<Tile, m, odd> {
    static const int offset = Tile::size() - static_mod_inverse<m, Tile::size()>::value;
    static const int rotate = static_mod_inverse<Tile::size(), m>::value;
    static const int permute = static_mod_inverse<rotate, m>::value;
};

template<typename Tile, int m>
struct c2r_offset_constants<Tile, m, power_of_two> {
    static const int offset = Tile::size() - Tile::size()/m;
    static const int permute = m - 1;
};

template<typename Tile, int m>
struct c2r_offset_constants<Tile, m, composite> {
    static const int c = static_gcd<m, Tile::size()>::value;
    static const int k = static_mod_inverse<m/c, Tile::size()/c>::value;
};

template<typename Tile, int m, typename Schema=typename tx_algorithm<m>::type>
struct r2c_offset_constants{};

template<typename Tile, int m>
struct r2c_offset_constants<Tile, m, odd> {
    static const int permute = static_mod_inverse<Tile::size(), m>::value;
};

template<typename Tile, int m>
struct r2c_offset_constants<Tile, m, power_of_two> :
        c2r_offset_constants<Tile, m, power_of_two> {
};

template<typename T, template<int> class Permute, int position=0>
struct tx_permute_impl{};

template<typename T, int s, template<int> class Permute, int position>
struct tx_permute_impl<array<T, s>, Permute, position> {
    typedef array<T, s> Remaining;
    static const int idx = Permute<position>::value;
    template<typename Source>
    __host__ __device__
    static Remaining impl(const Source& src) {
        return Remaining(
            trove::get<idx>(src),
            tx_permute_impl<array<T, s-1>, Permute, position+1>::impl(
                src));
    }
};

template<typename T, template<int> class Permute, int position>
struct tx_permute_impl<array<T, 1>, Permute, position> {
    typedef array<T, 1> Remaining;
    static const int idx = Permute<position>::value;
    template<typename Source>
    __host__ __device__
    static Remaining impl(const Source& src) {
        return Remaining(trove::get<idx>(src));
    }
};


template<int m, int a, int b=0>
struct affine_modular_fn {
    template<int x>
    struct eval {
        static const int value = (a * x + b) % m;
    };
};

template<typename Tile, int m>
struct composite_c2r_permute_fn {
    static const int o = Tile::size() % m;
    static const int c = static_gcd<m, Tile::size()>::value;
    static const int p = m / c;
    template<int x>
    struct eval {
        static const int value = (x * o - (x / p)) % m;
    };
};

template<typename Tile, int m>
struct composite_r2c_permute_fn {
    template<int x>
    struct eval {
        static const int value =
            inverse<int, composite_c2r_permute_fn<Tile, m>::template eval, x>::value;
    };
};


template<typename Tile, typename Array>
__host__ __device__ Array c2r_tx_permute(const Array& t) {
    return tx_permute_impl<
        Array,
        affine_modular_fn<Array::size,
        c2r_offset_constants<Tile, Array::size>::permute>::template eval>::impl(t);
}



template<typename Tile, typename Array>
__host__ __device__ Array composite_c2r_tx_permute(const Array& t) {
    return tx_permute_impl<
        Array,
        composite_c2r_permute_fn<Tile, Array::size>::template eval>::impl(t);
}

  template<typename Tile, typename Array>
__host__ __device__ Array composite_r2c_tx_permute(const Array& t) {
    return tx_permute_impl<
        Array,
        composite_r2c_permute_fn<Tile, Array::size>::template eval>::impl(t);
}



template<typename Tile, typename Array>
__host__ __device__ Array r2c_tx_permute(const Array& t) {
    return tx_permute_impl<
        Array,
        affine_modular_fn<Array::size,
                          r2c_offset_constants<Tile, Array::size>::permute>::template eval>::impl(t);
}


template<typename Array, int b, int o>
struct c2r_compute_offsets_impl{};

template<int s, int b, int o>
struct c2r_compute_offsets_impl<array<int, s>, b, o> {
    typedef array<int, s> Array;
    __device__
    static Array impl(int offset) {
        if (offset >= b) {
            offset -= b;
        } //Poor man's x % b. Requires that o < b.
        return Array(offset,
                     c2r_compute_offsets_impl<array<int, s-1>, b, o>::
                     impl(offset + o));
    }
};

template<int b, int o>
struct c2r_compute_offsets_impl<array<int, 1>, b, o> {
    typedef array<int, 1> Array;
    __device__
    static Array impl(int offset) {
        if (offset >= b) {
            offset -= b;
        } //Poor man's x % b. Requires that o < b.
        return Array(offset);
    }
};

template<typename Tile, int m, typename Schema>
struct c2r_compute_initial_offset {};

template<typename Tile, int m>
struct c2r_compute_initial_offset<Tile, m, odd> {
    typedef c2r_offset_constants<Tile, m> constants;
    __device__ static int impl(const Tile &tile) {
        int warp_id = tile.id();
        int initial_offset = ((Tile::size() - tile.id()) * constants::offset)
            & tile.mask();
        return initial_offset;
    }
};

template<typename Tile, int m>
struct c2r_compute_initial_offset<Tile, m, power_of_two> {
    __device__ static int impl(const Tile &tile) {
        int warp_id = tile.id();
        int initial_offset = ((warp_id * (tile.size() + 1)) >>
                              static_log<m>::value)
            & tile.mask();
        return initial_offset;
    }
};

template<int m, typename Schema>
struct r2c_compute_initial_offset {};

template<int m>
struct r2c_compute_initial_offset<m, odd> {
    template <typename Tile>
    __device__ static int impl(const Tile &tile) {
        int warp_id = tile.id();
        int initial_offset = (warp_id * m) & tile.mask();
        return initial_offset;
    }
};


template<typename Tile, int m, typename Schema>
__device__
array<int, m> c2r_compute_offsets(const Tile &tile) {
    typedef c2r_offset_constants<Tile, m> constants;
    int initial_offset = c2r_compute_initial_offset<Tile, m, Schema>::impl(tile);
    return c2r_compute_offsets_impl<array<int, m>,
                                    Tile::size(),
                                    constants::offset>::impl(initial_offset);
}

template<typename Tile, typename T, int m, int p = 0>
struct c2r_compute_composite_offsets{};

template<typename Tile, int s, int m, int p>
struct c2r_compute_composite_offsets<Tile, array<int, s>, m, p> {
    static const int n = Tile::size();
    static const int mod_n = n - 1;
    static const int c = static_gcd<m, Tile::size()>::value;
    static const int k = static_mod_inverse<m/c, n/c>::value;
    static const int mod_c = c - 1;
    static const int log_c = static_log<c>::value;
    static const int n_div_c = n / c;
    static const int mod_n_div_c = n_div_c - 1;
    static const int log_n_div_c = static_log<n_div_c>::value;
    typedef array<int, s> result_type;
    __host__ __device__ static result_type impl(int idx, int col) {
        int offset = ((((idx >> log_c) * k) & mod_n_div_c) +
                      ((idx & mod_c) << log_n_div_c)) & mod_n;
        int new_idx = idx + n - 1;
        new_idx = (p == m - c + (col & mod_c)) ? new_idx + m : new_idx;
        return
            result_type(offset,
                        c2r_compute_composite_offsets<Tile, array<int, s-1>, m, p+1>
            ::impl(new_idx, col));

    }
};

template<typename Tile, int m, int p>
struct c2r_compute_composite_offsets<Tile, array<int, 1>, m, p> {
  static const int n = Tile::size();
    static const int mod_n = n - 1;
    static const int c = static_gcd<m, Tile::size()>::value;
    static const int k = static_mod_inverse<m/c, n/c>::value;
    static const int mod_c = c - 1;
    static const int log_c = static_log<c>::value;
    static const int n_div_c = n / c;
    static const int mod_n_div_c = n_div_c - 1;
    static const int log_n_div_c = static_log<n_div_c>::value;
    typedef array<int, 1> result_type;
    __host__ __device__ static result_type impl(int idx, int col) {
        int offset = ((((idx >> log_c) * k) & mod_n_div_c) +
                      ((idx & mod_c) << log_n_div_c)) & mod_n;
        return result_type(offset);

    }
};


template<int index, int offset, int bound>
struct r2c_offsets {
    static const int value = (offset * index) % bound;
};

template<typename Tile, typename Array, int index, int m, typename Schema>
struct r2c_compute_offsets_impl{};

template<typename Tile, int s, int index, int m>
struct r2c_compute_offsets_impl<Tile, array<int, s>, index, m, odd> {
    typedef array<int, s> Array;
    static const int offset = (Tile::size() % m * index) % m;
    __device__
    static Array impl(int initial_offset) {
        int current_offset = (initial_offset + offset) & Tile::mask();
        return Array(current_offset,
                     r2c_compute_offsets_impl<Tile, array<int, s-1>,
                     index + 1, m, odd>::impl(initial_offset));
    }
};

template<typename Tile, int index, int m>
struct r2c_compute_offsets_impl<Tile, array<int, 1>, index, m, odd> {
    typedef array<int, 1> Array;
  static const int offset = (Tile::size() % m * index) % m;
    __device__
    static Array impl(int initial_offset) {
        int current_offset = (initial_offset + offset) & Tile::mask();
        return Array(current_offset);
    }
};


template<typename Tile, int s, int index, int m>
struct r2c_compute_offsets_impl<Tile, array<int, s>, index, m, power_of_two> {
    typedef array<int, s> Array;
    __device__
      static Array impl(int offset, int lb) {
      int new_offset = (offset == lb) ? offset + m - 1 : offset - 1;
        return Array(offset,
                     r2c_compute_offsets_impl<Tile, array<int, s-1>, index + 1, m, power_of_two>::impl(new_offset, lb));
    }
};

template<typename Tile, int index, int m>
struct r2c_compute_offsets_impl<Tile, array<int, 1>, index, m, power_of_two> {
    typedef array<int, 1> Array;
    __device__
    static Array impl(int offset, int lb) {
        return Array(offset);
    }
};


template<typename Tile, typename T, int m>
struct r2c_compute_composite_offsets{};

template<typename Tile, int s, int m>
struct r2c_compute_composite_offsets<Tile, array<int, s>, m> {
    static const int n = Tile::size();
    static const int mod_n = n - 1;
    static const int c = static_gcd<m, Tile::size()>::value;
    static const int n_div_c = n / c;
    static const int log_n_div_c = static_log<n_div_c>::value;
    typedef array<int, s> result_type;
    __host__ __device__ static result_type impl(int col, int offset, int lb, int ub) {
        int new_offset = offset + 1;
        new_offset = (new_offset == ub) ? lb : new_offset;
        return
            result_type(offset & mod_n,
                        r2c_compute_composite_offsets<Tile, array<int, s-1>, m>
                        ::impl(col, new_offset, lb, ub));

    }
};

template<typename Tile, int m>
struct r2c_compute_composite_offsets<Tile, array<int, 1>, m> {
    static const int n = Tile::size();
    static const int mod_n = n - 1;
    static const int c = static_gcd<m, Tile::size()>::value;
    static const int n_div_c = n / c;
    static const int log_n_div_c = static_log<n_div_c>::value;
    typedef array<int, 1> result_type;
    __host__ __device__ static result_type impl(int col, int offset, int lb, int ub) {
        return  result_type(offset & mod_n);

    }
};

template<typename Tile,int m, typename Schema>
__device__
array<int, m> r2c_compute_offsets(const Tile &tile) {
  typedef r2c_offset_constants<Tile, m> constants;
    typedef array<int, m> result_type;
    int initial_offset = r2c_compute_initial_offset<m, Schema>::impl(tile);
    return r2c_compute_offsets_impl<Tile, result_type,
                                    0, m, Schema>::impl(initial_offset);
}


template<typename Data, typename Indices>
struct warp_shuffle {};

template<typename T, int m>
struct warp_shuffle<array<T, m>, array<int, m>> {
    template <typename Tile>
    __device__ static void impl(array<T, m>& d,
                                const array<int, m>& i,
                                const Tile &tile) {
        d.head = tile.shfl(d.head, i.head);
        warp_shuffle<array<T, m-1>, array<int, m-1> >::impl(d.tail,
                                                            i.tail,
                                                            tile);
    }
};

template<typename T>
struct warp_shuffle<array<T, 1>, array<int, 1>> {
    template <typename Tile>
    __device__ static void impl(array<T, 1>& d,
                                const array<int, 1>& i,
                                const Tile &tile) {
        d.head = tile.shfl(d.head, i.head);
    }
};


template<typename Array, typename Schema>
struct c2r_compute_indices_impl {};

template<typename Array>
struct c2r_compute_indices_impl<Array, odd> {
    template <typename Tile>
    __device__ static void impl(Array& indices, int& rotation, const Tile &tile) {
        indices = detail::c2r_compute_offsets<Tile, Array::size, odd>(tile);
        int warp_id = tile.id();
        int size = Array::size;
        int r = detail::c2r_offset_constants<Tile, Array::size>::rotate;
        rotation = (warp_id * r) % size;
    }
};

template<typename Array>
struct c2r_compute_indices_impl<Array, power_of_two> {
    template <typename Tile>
    __device__ static void impl(Array& indices, int& rotation, const Tile &tile) {
        indices = detail::c2r_compute_offsets<Tile, Array::size, power_of_two>(tile);
        int warp_id = tile.id();
        int size = Array::size;
        rotation = (size - warp_id) & (size - 1);
    }
};

template<typename Array>
struct c2r_compute_indices_impl<Array, composite> {
    template <typename Tile>
    __device__ static void impl(Array& indices, int& rotation, const Tile &tile) {
        int warp_id = tile.id();
        indices = detail::c2r_compute_composite_offsets<Tile, Array, Array::size>::
            impl(warp_id, warp_id);
        rotation = warp_id % Array::size;
    }
};

template<typename Array, typename Indices, typename Schema>
struct c2r_warp_transpose_impl {};

template<typename Array, typename Indices>
struct c2r_warp_transpose_impl<Array, Indices, odd> {
    template <typename Tile>
    __device__ static void impl(Array& src,
                                const Indices& indices,
                                const int& rotation,
                                const Tile &tile) {
        detail::warp_shuffle<Array, Indices>::impl(src, indices, tile);
        src = rotate(detail::c2r_tx_permute<Tile>(src), rotation);
    }
};

template<typename Array, typename Indices>
struct c2r_warp_transpose_impl<Array, Indices, power_of_two> {
    template <typename Tile>
    __device__ static void impl(Array& src,
                                const Indices& indices,
                                const int& rotation,
                                const Tile &tile) {
        int warp_id = tile.id();
        int pre_rotation = warp_id >>
            (tile.log_size()-
             static_log<Array::size>::value);
        src = rotate(src, pre_rotation);
        c2r_warp_transpose_impl<Array, Indices, odd>::impl
            (src, indices, rotation, tile);
    }
};

template<typename Array, typename Indices>
struct c2r_warp_transpose_impl<Array, Indices, composite> {
    template <typename Tile>
    __device__ static void impl(Array& src,
                                const Indices& indices,
                                const int& rotation,
                                const Tile &tile) {
        int warp_id = tile.id();
        int pre_rotation = warp_id >> static_log<Tile::size()/static_gcd<Array::size, Tile::size()>::value>::value;
        src = rotate(src, pre_rotation);
        detail::warp_shuffle<Array, Indices>::impl(src, indices, tile);
        src = rotate(src, rotation);
        src = composite_c2r_tx_permute<Tile>(src);
    }
};

template<typename Tile, typename Array, typename Schema>
struct r2c_compute_indices_impl {};

template<typename Tile, typename Array>
struct r2c_compute_indices_impl<Tile, Array, odd> {

    __device__ static void impl(Array& indices, int& rotation, const Tile &tile) {
        indices =
            detail::r2c_compute_offsets<Tile, Array::size, odd>(tile);
        int warp_id = tile.id();
        int size = Array::size;
        int r =
            size - detail::r2c_offset_constants<Tile, Array::size>::permute;
        rotation = (warp_id * r) % size;
    }
};

template<typename Tile, typename Array>
struct r2c_compute_indices_impl<Tile, Array, power_of_two> {
    static const int m = Array::size;
    static const int log_m = static_log<m>::value;
    static const int clear_m = ~(m-1);
    static const int n = Tile::size();
    static const int log_n = static_log<n>::value;
    static const int mod_n = n-1;
    static const int n_div_m = Tile::size() / m;
    static const int log_n_div_m = static_log<n_div_m>::value;
    __device__ static void impl(Array& indices, int& rotation, const Tile &tile) {
        int warp_id = tile.id();
        int size = Array::size;
        rotation = warp_id % size;
        int initial_offset = ((warp_id << log_m) + (warp_id >> log_n_div_m)) & mod_n;
        int lb = initial_offset & clear_m;
        indices = r2c_compute_offsets_impl<Tile, Array, 0,
          Array::size, power_of_two>::impl(initial_offset, lb);
    }
};

template<typename Tile, typename Array>
struct r2c_compute_indices_impl<Tile, Array, composite> {
    static const int size = Array::size;
    static const int c = static_gcd<size, Tile::size()>::value;
    __device__ static void impl(Array& indices, int& rotation, const Tile &tile) {
        int warp_id = tile.id();
        rotation = size - (warp_id % size);
        int lb = (size * warp_id) & tile.mask();
        int ub = lb + size;
        int offset = lb + warp_id / (tile.size()/c);
        indices = detail::r2c_compute_composite_offsets<Tile, Array, Array::size>::
            impl(warp_id, offset, lb, ub);
    }
};

template<typename Array, typename Indices, typename Schema>
struct r2c_warp_transpose_impl {};

template<typename Array, typename Indices>
struct r2c_warp_transpose_impl<Array, Indices, odd> {
    template <typename Tile>
    __device__ static void impl(Array& src,
                                const Indices& indices,
                                const int& rotation,
                                const Tile &tile) {
        Array rotated = rotate(src, rotation);
        detail::warp_shuffle<Array, Indices>::impl(rotated, indices, tile);
        src = detail::r2c_tx_permute<Tile>(rotated);
    }
};

template<typename Array, typename Indices>
struct r2c_warp_transpose_impl<Array, Indices, power_of_two> {
    template <typename Tile>
    __device__ static void impl(Array& src,
                                const Indices& indices,
                                const int& rotation,
                                const Tile &tile) {
        Array rotated = rotate(src, rotation);
        detail::warp_shuffle<Array, Indices>::impl(rotated, indices, tile);
        const int size = Array::size;
        int warp_id = tile.id();
        src = rotate(detail::r2c_tx_permute<Tile>(rotated),
                     (size-warp_id/(Tile::size()/size))%size);
    }
};

template<typename Array, typename Indices>
struct r2c_warp_transpose_impl<Array, Indices, composite> {
    static const int size = Array::size;
    template <typename Tile>
    __device__ static void impl(Array& src,
                                const Indices& indices,
                                const int& rotation,
                                const Tile &tile) {
        constexpr int c = static_gcd<Array::size, Tile::size()>::value;
        int warp_id = tile.id();
        src = composite_r2c_tx_permute<Tile>(src);
        src = rotate(src, rotation);
        detail::warp_shuffle<Array, Indices>::impl(src, indices, tile);
        src = rotate(src, size - (warp_id/(Tile::size()/c)));
    }
};

} //end namespace detail

template<typename Tile, int i>
__device__ void c2r_compute_indices(array<int, i>& indices, int& rotation, const Tile &tile) {
    typedef array<int, i> Array;
    detail::c2r_compute_indices_impl<
        Array,
        typename detail::tx_algorithm<i>::type>
        ::impl(indices, rotation, tile);
}

template<typename Tile, typename T, int i>
__device__ void c2r_warp_transpose(array<T, i>& src,
                                   const array<int, i>& indices,
                                   int rotation,
                                   const Tile &tile) {
    typedef array<T, i> Array;
    detail::c2r_warp_transpose_impl<
        Array, array<int, i>,
        typename detail::tx_algorithm<i>::type>::
        impl(src, indices, rotation, tile);
}

template<typename Tile, typename T, int i>
__device__ void c2r_warp_transpose(array<T, i>& src, const Tile &tile) {
    typedef array<T, i> Array;
    typedef array<int, i> indices_array;
    indices_array indices;
    int rotation;
    c2r_compute_indices(indices, rotation, tile);

    detail::c2r_warp_transpose_impl<
        Array, array<int, i>,
        typename detail::tx_algorithm<i>::type>::
        impl(src, indices, rotation, tile);
}

template<typename Tile, int i>
__device__ void r2c_compute_indices(array<int, i>& indices, int& rotation, const Tile &tile) {
    typedef array<int, i> Array;
    detail::r2c_compute_indices_impl<
        Tile, Array, typename detail::tx_algorithm<i>::type>
        ::impl(indices, rotation, tile);
}

template<typename Tile, typename T, int i>
__device__ void r2c_warp_transpose(array<T, i>& src,
                                   const array<int, i>& indices,
                                   int rotation,
                                   const Tile &tile) {
    typedef array<T, i> Array;
    detail::r2c_warp_transpose_impl<
        Array, array<int, i>,
        typename detail::tx_algorithm<i>::type>
    ::impl(src, indices, rotation, tile);
}

template<typename Tile, typename T, int i>
__device__ void r2c_warp_transpose(array<T, i>& src, const Tile &tile) {
    typedef array<T, i> Array;
    typedef array<int, i> indices_array;
    indices_array indices;
    int rotation;
    r2c_compute_indices<Tile>(indices, rotation, tile);

    detail::r2c_warp_transpose_impl<
        Array, array<int, i>,
        typename detail::tx_algorithm<i>::type>
    ::impl(src, indices, rotation, tile);
}

} //end namespace trove
