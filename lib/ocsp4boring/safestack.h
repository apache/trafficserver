#pragma once

# ifndef CHECKED_PTR_OF
#  define CHECKED_PTR_OF(type, p) \
    ((void*) (1 ? p : (type*)0))
# endif

/*
 * In C++ we get problems because an explicit cast is needed from (void *) we
 * use CHECKED_STACK_OF to ensure the correct type is passed in the macros
 * below.
 */

# define CHECKED_STACK_OF(type, p) \
    ((_STACK*) (1 ? p : (STACK_OF(type)*)0))

# define CHECKED_SK_COPY_FUNC(type, p) \
    ((void *(*)(void *)) ((1 ? p : (type *(*)(const type *))0)))

# define CHECKED_SK_FREE_FUNC(type, p) \
    ((void (*)(void *)) ((1 ? p : (void (*)(type *))0)))

# define CHECKED_SK_CMP_FUNC(type, p) \
    ((int (*)(const void *, const void *)) \
        ((1 ? p : (int (*)(const type * const *, const type * const *))0)))

#define SKM_sk_new(type, cmp) \
        ((STACK_OF(type) *)sk_new(CHECKED_SK_CMP_FUNC(type, cmp)))
# define SKM_sk_new_null(type) \
        ((STACK_OF(type) *)sk_new_null())
# define SKM_sk_free(type, st) \
        sk_free(CHECKED_STACK_OF(type, st))
# define SKM_sk_num(type, st) \
        sk_num(CHECKED_STACK_OF(type, st))
# define SKM_sk_value(type, st,i) \
        ((type *)sk_value(CHECKED_STACK_OF(type, st), i))
# define SKM_sk_set(type, st,i,val) \
        sk_set(CHECKED_STACK_OF(type, st), i, CHECKED_PTR_OF(type, val))
# define SKM_sk_zero(type, st) \
        sk_zero(CHECKED_STACK_OF(type, st))
# define SKM_sk_push(type, st, val) \
        sk_push(CHECKED_STACK_OF(type, st), CHECKED_PTR_OF(type, val))
//# define sk_OCSP_SINGLERESP_num(st) SKM_sk_num(OCSP_SINGLERESP, (st))
# define sk_OCSP_CERTID_new(cmp) SKM_sk_new(OCSP_CERTID, (cmp))
# define sk_OCSP_CERTID_new_null() SKM_sk_new_null(OCSP_CERTID)
# define sk_OCSP_CERTID_free(st) SKM_sk_free(OCSP_CERTID, (st))
# define sk_OCSP_CERTID_num(st) SKM_sk_num(OCSP_CERTID, (st))
# define sk_OCSP_CERTID_value(st, i) SKM_sk_value(OCSP_CERTID, (st), (i))
# define sk_OCSP_CERTID_set(st, i, val) SKM_sk_set(OCSP_CERTID, (st), (i), (val))
# define sk_OCSP_CERTID_zero(st) SKM_sk_zero(OCSP_CERTID, (st))
# define sk_OCSP_CERTID_push(st, val) SKM_sk_push(OCSP_CERTID, (st), (val))
# define sk_OCSP_CERTID_unshift(st, val) SKM_sk_unshift(OCSP_CERTID, (st), (val))
# define sk_OCSP_CERTID_find(st, val) SKM_sk_find(OCSP_CERTID, (st), (val))
# define sk_OCSP_CERTID_find_ex(st, val) SKM_sk_find_ex(OCSP_CERTID, (st), (val))
# define sk_OCSP_CERTID_delete(st, i) SKM_sk_delete(OCSP_CERTID, (st), (i))
# define sk_OCSP_CERTID_delete_ptr(st, ptr) SKM_sk_delete_ptr(OCSP_CERTID, (st), (ptr))
# define sk_OCSP_CERTID_insert(st, val, i) SKM_sk_insert(OCSP_CERTID, (st), (val), (i))
# define sk_OCSP_CERTID_set_cmp_func(st, cmp) SKM_sk_set_cmp_func(OCSP_CERTID, (st), (cmp))
# define sk_OCSP_CERTID_dup(st) SKM_sk_dup(OCSP_CERTID, st)
# define sk_OCSP_CERTID_pop_free(st, free_func) SKM_sk_pop_free(OCSP_CERTID, (st), (free_func))
# define sk_OCSP_CERTID_deep_copy(st, copy_func, free_func) SKM_sk_deep_copy(OCSP_CERTID, (st), (copy_func), (free_func))
# define sk_OCSP_CERTID_shift(st) SKM_sk_shift(OCSP_CERTID, (st))
# define sk_OCSP_CERTID_pop(st) SKM_sk_pop(OCSP_CERTID, (st))
# define sk_OCSP_CERTID_sort(st) SKM_sk_sort(OCSP_CERTID, (st))
# define sk_OCSP_CERTID_is_sorted(st) SKM_sk_is_sorted(OCSP_CERTID, (st))
# define sk_OCSP_ONEREQ_new(cmp) SKM_sk_new(OCSP_ONEREQ, (cmp))
# define sk_OCSP_ONEREQ_new_null() SKM_sk_new_null(OCSP_ONEREQ)
# define sk_OCSP_ONEREQ_free(st) SKM_sk_free(OCSP_ONEREQ, (st))
# define sk_OCSP_ONEREQ_num(st) SKM_sk_num(OCSP_ONEREQ, (st))
# define sk_OCSP_ONEREQ_value(st, i) SKM_sk_value(OCSP_ONEREQ, (st), (i))
# define sk_OCSP_ONEREQ_set(st, i, val) SKM_sk_set(OCSP_ONEREQ, (st), (i), (val))
# define sk_OCSP_ONEREQ_zero(st) SKM_sk_zero(OCSP_ONEREQ, (st))
# define sk_OCSP_ONEREQ_push(st, val) SKM_sk_push(OCSP_ONEREQ, (st), (val))
# define sk_OCSP_ONEREQ_unshift(st, val) SKM_sk_unshift(OCSP_ONEREQ, (st), (val))
# define sk_OCSP_ONEREQ_find(st, val) SKM_sk_find(OCSP_ONEREQ, (st), (val))
# define sk_OCSP_ONEREQ_find_ex(st, val) SKM_sk_find_ex(OCSP_ONEREQ, (st), (val))
# define sk_OCSP_ONEREQ_delete(st, i) SKM_sk_delete(OCSP_ONEREQ, (st), (i))
# define sk_OCSP_ONEREQ_delete_ptr(st, ptr) SKM_sk_delete_ptr(OCSP_ONEREQ, (st), (ptr))
# define sk_OCSP_ONEREQ_insert(st, val, i) SKM_sk_insert(OCSP_ONEREQ, (st), (val), (i))
# define sk_OCSP_ONEREQ_set_cmp_func(st, cmp) SKM_sk_set_cmp_func(OCSP_ONEREQ, (st), (cmp))
# define sk_OCSP_ONEREQ_dup(st) SKM_sk_dup(OCSP_ONEREQ, st)
# define sk_OCSP_ONEREQ_pop_free(st, free_func) SKM_sk_pop_free(OCSP_ONEREQ, (st), (free_func))
# define sk_OCSP_ONEREQ_deep_copy(st, copy_func, free_func) SKM_sk_deep_copy(OCSP_ONEREQ, (st), (copy_func), (free_func))
# define sk_OCSP_ONEREQ_shift(st) SKM_sk_shift(OCSP_ONEREQ, (st))
# define sk_OCSP_ONEREQ_pop(st) SKM_sk_pop(OCSP_ONEREQ, (st))
# define sk_OCSP_ONEREQ_sort(st) SKM_sk_sort(OCSP_ONEREQ, (st))
# define sk_OCSP_ONEREQ_is_sorted(st) SKM_sk_is_sorted(OCSP_ONEREQ, (st))
# define sk_OCSP_RESPID_new(cmp) SKM_sk_new(OCSP_RESPID, (cmp))
# define sk_OCSP_RESPID_new_null() SKM_sk_new_null(OCSP_RESPID)
# define sk_OCSP_RESPID_free(st) SKM_sk_free(OCSP_RESPID, (st))
# define sk_OCSP_RESPID_num(st) SKM_sk_num(OCSP_RESPID, (st))
# define sk_OCSP_RESPID_value(st, i) SKM_sk_value(OCSP_RESPID, (st), (i))
# define sk_OCSP_RESPID_set(st, i, val) SKM_sk_set(OCSP_RESPID, (st), (i), (val))
# define sk_OCSP_RESPID_zero(st) SKM_sk_zero(OCSP_RESPID, (st))
# define sk_OCSP_RESPID_push(st, val) SKM_sk_push(OCSP_RESPID, (st), (val))
# define sk_OCSP_RESPID_unshift(st, val) SKM_sk_unshift(OCSP_RESPID, (st), (val))
# define sk_OCSP_RESPID_find(st, val) SKM_sk_find(OCSP_RESPID, (st), (val))
# define sk_OCSP_RESPID_find_ex(st, val) SKM_sk_find_ex(OCSP_RESPID, (st), (val))
# define sk_OCSP_RESPID_delete(st, i) SKM_sk_delete(OCSP_RESPID, (st), (i))
# define sk_OCSP_RESPID_delete_ptr(st, ptr) SKM_sk_delete_ptr(OCSP_RESPID, (st), (ptr))
# define sk_OCSP_RESPID_insert(st, val, i) SKM_sk_insert(OCSP_RESPID, (st), (val), (i))
# define sk_OCSP_RESPID_set_cmp_func(st, cmp) SKM_sk_set_cmp_func(OCSP_RESPID, (st), (cmp))
# define sk_OCSP_RESPID_dup(st) SKM_sk_dup(OCSP_RESPID, st)
# define sk_OCSP_RESPID_pop_free(st, free_func) SKM_sk_pop_free(OCSP_RESPID, (st), (free_func))
# define sk_OCSP_RESPID_deep_copy(st, copy_func, free_func) SKM_sk_deep_copy(OCSP_RESPID, (st), (copy_func), (free_func))
# define sk_OCSP_RESPID_shift(st) SKM_sk_shift(OCSP_RESPID, (st))
# define sk_OCSP_RESPID_pop(st) SKM_sk_pop(OCSP_RESPID, (st))
# define sk_OCSP_RESPID_sort(st) SKM_sk_sort(OCSP_RESPID, (st))
# define sk_OCSP_RESPID_is_sorted(st) SKM_sk_is_sorted(OCSP_RESPID, (st))
# define sk_OCSP_SINGLERESP_new(cmp) SKM_sk_new(OCSP_SINGLERESP, (cmp))
# define sk_OCSP_SINGLERESP_new_null() SKM_sk_new_null(OCSP_SINGLERESP)
# define sk_OCSP_SINGLERESP_free(st) SKM_sk_free(OCSP_SINGLERESP, (st))
# define sk_OCSP_SINGLERESP_num(st) SKM_sk_num(OCSP_SINGLERESP, (st))
# define sk_OCSP_SINGLERESP_value(st, i) SKM_sk_value(OCSP_SINGLERESP, (st), (i))
# define sk_OCSP_SINGLERESP_set(st, i, val) SKM_sk_set(OCSP_SINGLERESP, (st), (i), (val))
# define sk_OCSP_SINGLERESP_zero(st) SKM_sk_zero(OCSP_SINGLERESP, (st))
# define sk_OCSP_SINGLERESP_push(st, val) SKM_sk_push(OCSP_SINGLERESP, (st), (val))
# define sk_OCSP_SINGLERESP_unshift(st, val) SKM_sk_unshift(OCSP_SINGLERESP, (st), (val))
# define sk_OCSP_SINGLERESP_find(st, val) SKM_sk_find(OCSP_SINGLERESP, (st), (val))
# define sk_OCSP_SINGLERESP_find_ex(st, val) SKM_sk_find_ex(OCSP_SINGLERESP, (st), (val))
# define sk_OCSP_SINGLERESP_delete(st, i) SKM_sk_delete(OCSP_SINGLERESP, (st), (i))
# define sk_OCSP_SINGLERESP_delete_ptr(st, ptr) SKM_sk_delete_ptr(OCSP_SINGLERESP, (st), (ptr))
# define sk_OCSP_SINGLERESP_insert(st, val, i) SKM_sk_insert(OCSP_SINGLERESP, (st), (val), (i))
# define sk_OCSP_SINGLERESP_set_cmp_func(st, cmp) SKM_sk_set_cmp_func(OCSP_SINGLERESP, (st), (cmp))
# define sk_OCSP_SINGLERESP_dup(st) SKM_sk_dup(OCSP_SINGLERESP, st)
# define sk_OCSP_SINGLERESP_pop_free(st, free_func) SKM_sk_pop_free(OCSP_SINGLERESP, (st), (free_func))
# define sk_OCSP_SINGLERESP_deep_copy(st, copy_func, free_func) SKM_sk_deep_copy(OCSP_SINGLERESP, (st), (copy_func), (free_func))
# define sk_OCSP_SINGLERESP_shift(st) SKM_sk_shift(OCSP_SINGLERESP, (st))
# define sk_OCSP_SINGLERESP_pop(st) SKM_sk_pop(OCSP_SINGLERESP, (st))
# define sk_OCSP_SINGLERESP_sort(st) SKM_sk_sort(OCSP_SINGLERESP, (st))
# define sk_OCSP_SINGLERESP_is_sorted(st) SKM_sk_is_sorted(OCSP_SINGLERESP, (st))

