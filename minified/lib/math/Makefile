# SPDX-License-Identifier: GPL-2.0-only
obj-y += div64.o gcd.o
# reciprocal_div.o merged into gcd.c

obj-$(CONFIG_CORDIC)		+= cordic.o
obj-$(CONFIG_PRIME_NUMBERS)	+= prime_numbers.o
obj-$(CONFIG_RATIONAL)		+= rational.o

obj-$(CONFIG_TEST_DIV64)	+= test_div64.o
obj-$(CONFIG_RATIONAL_KUNIT_TEST) += rational-test.o
