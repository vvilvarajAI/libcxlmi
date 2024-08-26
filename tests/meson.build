# SPDX-License-Identifier: LGPL-2.1-or-later
#
# This file is part of libcxlmi.
#

# Add the gtest folder to the include directories
gtest_inc = include_directories('path/to/gtest')

executable(
    'api-simple-tests',
    ['api-simple-tests.c'],
    dependencies: libcxlmi_dep,
    include_directories: [inc, gtest_inc],
    link_with: gtest_libs
)
