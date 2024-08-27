# SPDX-License-Identifier: LGPL-2.1-or-later
#
# This file is part of libcxlmi.
#

# Define the gtest_libs variable
gtest_libs = dependency('gtest')
# Add the gtest folder to the include directories
gtest_inc = include_directories('/usr/src/googletest/googletest/include')

executable(
    'api-simple-tests',
    ['api-simple-tests.c'],
    dependencies: [libcxlmi_dep, gtest_libs],
    include_directories: [inc, gtest_inc]
)
