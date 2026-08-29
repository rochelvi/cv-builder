# The core has no window and no operating system in it, so it can be tested
# without either. Nothing here links a UI target - if it ever needs to, the
# layering has been broken.
add_executable(resume_tests
               harness.cpp
               test_numeric.cpp
               test_file.cpp)
target_link_libraries(resume_tests PRIVATE resume_core)

add_test(NAME core COMMAND resume_tests)
