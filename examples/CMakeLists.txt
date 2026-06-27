add_executable(pubsub_example pubsub_example.cpp)
target_link_libraries(pubsub_example PRIVATE lib::process_communications)

# Two standalone processes that talk to each other over TCP.
add_executable(subscriber subscriber.cpp)
target_link_libraries(subscriber PRIVATE lib::process_communications)

add_executable(publisher publisher.cpp)
target_link_libraries(publisher PRIVATE lib::process_communications)
