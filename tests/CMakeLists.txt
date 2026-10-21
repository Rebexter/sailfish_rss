find_package(Qt5 COMPONENTS Test REQUIRED)

# Runs on every build (RPM %check)
add_executable(tst_greaderparser tst_greaderparser.cpp)
target_link_libraries(tst_greaderparser sailrss-core Qt5::Test)
add_test(NAME greaderparser COMMAND tst_greaderparser)

add_executable(tst_database tst_database.cpp)
target_link_libraries(tst_database sailrss-core Qt5::Test)
add_test(NAME database COMMAND tst_database)

add_executable(tst_articlehtml tst_articlehtml.cpp)
target_link_libraries(tst_articlehtml sailrss-core Qt5::Test)
add_test(NAME articlehtml COMMAND tst_articlehtml)

# Needs a FreshRSS server; run manually with scripts/dev.sh live-test
add_executable(tst_greaderlive tst_greaderlive.cpp)
target_link_libraries(tst_greaderlive sailrss-core Qt5::Test)

add_executable(tst_synclive tst_synclive.cpp)
target_link_libraries(tst_synclive sailrss-core Qt5::Test)
