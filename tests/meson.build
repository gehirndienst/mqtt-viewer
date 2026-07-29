test_inc = include_directories('../src')

test_spsc = executable('test_spsc_queue',
    'test_spsc_queue.c', '../src/model/spsc_queue.c',
    include_directories : test_inc
)
test('spsc_queue', test_spsc)

test_pool = executable('test_pool_alloc',
    'test_pool_alloc.c', '../src/model/pool_alloc.c',
    include_directories : test_inc
)
test('pool_alloc', test_pool)

test_arena = executable('test_arena_alloc',
    'test_arena_alloc.c', '../src/model/arena_alloc.c',
    include_directories : test_inc
)
test('arena_alloc', test_arena)

test_topic = executable('test_topic_tree',
    'test_topic_tree.c', '../src/model/topic_tree.c',
    '../src/model/pool_alloc.c', '../src/model/arena_alloc.c',
    '../src/model/message_buf.c',
    include_directories : test_inc
)
test('topic_tree', test_topic)

test_msgbuf = executable('test_message_buf',
    'test_message_buf.c', '../src/model/message_buf.c',
    include_directories : test_inc
)
test('message_buf', test_msgbuf)

test_connlog = executable('test_connection_log',
    'test_connection_log.c', '../src/model/connection_log.c',
    include_directories : test_inc
)
test('connection_log', test_connlog)

test_sub = executable('test_subscription',
    'test_subscription.c', '../src/model/subscription.c',
    include_directories : test_inc
)
test('subscription', test_sub)

test_textinput = executable('test_text_input',
    'test_text_input.c',
    include_directories : test_inc,
    dependencies : raylib_dep.partial_dependency(compile_args : true, includes : true)
)
test('text_input', test_textinput)
