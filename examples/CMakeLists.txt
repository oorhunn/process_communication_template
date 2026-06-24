add_executable(pubsub_example pubsub_example.cpp)
target_include_directories(pubsub_example PRIVATE ${ROOT_PATH})

# Two standalone processes that talk to each other over TCP.
add_executable(subscriber subscriber.cpp)
target_include_directories(subscriber PRIVATE ${ROOT_PATH})

add_executable(publisher publisher.cpp)
target_include_directories(publisher PRIVATE ${ROOT_PATH})
