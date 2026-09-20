#define IR_ROUTE_TEST
#define main ir_route_program_main
#include "../cbridge/ir-route.c"
#undef main

#include <assert.h>
#include <sys/types.h>

struct mock_device {
	RouteGraph *graph;
	unsigned calls;
	unsigned fail_call;
	unsigned fail_call2;
	unsigned operation_count;
	bool actual[2];
	struct {
		unsigned index;
		bool enabled;
	} operations[16];
};

static struct mock_device *active_mock;

static int mock_setup_link(RouteGraph *graph, unsigned index, bool enabled)
{
	struct mock_device *mock = active_mock;

	(void)graph;
	assert(mock != NULL);
	assert(index < 2U);
	assert(mock->operation_count < 16U);
	mock->calls++;
	mock->operations[mock->operation_count].index = index;
	mock->operations[mock->operation_count].enabled = enabled;
	mock->operation_count++;
	if ((mock->fail_call != 0U && mock->calls == mock->fail_call) ||
		(mock->fail_call2 != 0U && mock->calls == mock->fail_call2))
		return 2;
	mock->actual[index] = enabled;
	return 0;
}

static void init_graph(RouteGraph *graph, bool link0, bool link1)
{
	memset(graph, 0, sizeof(*graph));
	snprintf(graph->path, sizeof(graph->path), "/dev/mock-media0");
	graph->entity_count = 3U;
	graph->entities[0].id = 10U;
	graph->entities[1].id = 11U;
	graph->entities[2].id = 12U;
	snprintf(graph->entities[0].name, sizeof(graph->entities[0].name),
		"ov7251 2-0060");
	snprintf(graph->entities[1].name, sizeof(graph->entities[1].name),
		"Intel IPU4 CSI-2 1");
	snprintf(graph->entities[2].name, sizeof(graph->entities[2].name),
		"Intel IPU4 CSI-2 1 capture 0");
	snprintf(graph->links[0].source, sizeof(graph->links[0].source),
		"ov7251 2-0060");
	snprintf(graph->links[0].sink, sizeof(graph->links[0].sink),
		"Intel IPU4 CSI-2 1");
	graph->links[0].source_pad = 0U;
	graph->links[0].sink_pad = 0U;
	graph->links[0].enabled = link0;
	snprintf(graph->links[1].source, sizeof(graph->links[1].source),
		"Intel IPU4 CSI-2 1");
	snprintf(graph->links[1].sink, sizeof(graph->links[1].sink),
		"Intel IPU4 CSI-2 1 capture 0");
	graph->links[1].source_pad = 1U;
	graph->links[1].sink_pad = 0U;
	graph->links[1].enabled = link1;
}

static void init_state(RouteState *state, bool link0, bool link1)
{
	RouteGraph graph;

	init_graph(&graph, link0, link1);
	memset(state, 0, sizeof(*state));
	snprintf(state->media, sizeof(state->media), "%s", graph.path);
	state->links[0] = graph.links[0];
	state->links[1] = graph.links[1];
}

static void make_state_file(char *path, size_t path_size,
	const RouteState *state)
{
	RouteGraph graph;
	int fd;

	init_graph(&graph, state->links[0].enabled, state->links[1].enabled);
	graph.links[0] = state->links[0];
	graph.links[1] = state->links[1];
	snprintf(path, path_size, "/var/tmp/ir-route-mock-state-XXXXXX");
	fd = mkstemp(path);
	assert(fd >= 0);
	close(fd);
	unlink(path);
	assert(write_state(path, &graph) == 0);
}

static void reset_mock(RouteGraph *graph, unsigned fail_call)
{
	static struct mock_device mock;

	memset(&mock, 0, sizeof(mock));
	mock.graph = graph;
	mock.fail_call = fail_call;
	mock.actual[0] = graph->links[0].enabled;
	mock.actual[1] = graph->links[1].enabled;
	active_mock = &mock;
	setup_link_override = mock_setup_link;
}

static void assert_op(const struct mock_device *mock, unsigned position,
	unsigned index, bool enabled)
{
	assert(position < mock->operation_count);
	assert(mock->operations[position].index == index);
	assert(mock->operations[position].enabled == enabled);
}

static struct mock_device *mock_state(void)
{
	return active_mock;
}

static void test_partial_prepare_failure(void)
{
	RouteGraph graph;
	char path[PATH_MAX];
	struct mock_device *mock;

	init_graph(&graph, false, false);
	snprintf(path, sizeof(path), "/var/tmp/ir-route-prepare-XXXXXX");
	{
		int fd = mkstemp(path);

		assert(fd >= 0);
		close(fd);
		unlink(path);
	}
	reset_mock(&graph, 2U);
	assert(prepare_route(&graph, path) != 0);
	mock = mock_state();
	assert(mock->operation_count == 4U);
	assert_op(mock, 0U, 0U, true);
	assert_op(mock, 1U, 1U, true);
	assert_op(mock, 2U, 1U, false);
	assert_op(mock, 3U, 0U, false);
	assert(!mock->actual[0] && !mock->actual[1]);
	assert(access(path, F_OK) != 0);
}

static void test_partial_prepare_retains_state(void)
{
	RouteGraph graph;
	char path[PATH_MAX];

	init_graph(&graph, false, false);
	snprintf(path, sizeof(path), "/var/tmp/ir-route-prepare-retain-XXXXXX");
	{
		int fd = mkstemp(path);

		assert(fd >= 0);
		close(fd);
		unlink(path);
	}
	reset_mock(&graph, 2U);
	mock_state()->fail_call2 = 3U;
	assert(prepare_route(&graph, path) != 0);
	assert(!mock_state()->actual[0] && !mock_state()->actual[1]);
	assert(access(path, F_OK) == 0);
	unlink(path);
}

static void test_partial_restore_failure_and_retry(void)
{
	RouteGraph graph;
	RouteState state;
	char path[PATH_MAX];
	struct mock_device *mock;

	init_state(&state, false, false);
	make_state_file(path, sizeof(path), &state);
	init_graph(&graph, true, true);
	reset_mock(&graph, 2U);
	assert(restore_route(&graph, &state, path) != 0);
	mock = mock_state();
	assert(mock->operation_count == 2U);
	assert_op(mock, 0U, 1U, false);
	assert_op(mock, 1U, 0U, false);
	assert(!mock->actual[1] && mock->actual[0]);
	assert(access(path, F_OK) == 0);

	/* A retry rediscovers the graph, so feed the mocked device's actual state
	 * back into the next RouteGraph just as graph_discover would. */
	graph.links[0].enabled = mock->actual[0];
	graph.links[1].enabled = mock->actual[1];
	reset_mock(&graph, 0U);
	assert(restore_route(&graph, &state, path) == 0);
	mock = mock_state();
	assert(mock->operation_count == 1U);
	assert_op(mock, 0U, 0U, false);
	assert(!mock->actual[0] && !mock->actual[1]);
	assert(access(path, F_OK) != 0);
}

static void test_already_restored_links(void)
{
	RouteGraph graph;
	RouteState state;
	char path[PATH_MAX];
	struct mock_device *mock;

	init_state(&state, false, false);
	make_state_file(path, sizeof(path), &state);
	init_graph(&graph, false, false);
	reset_mock(&graph, 0U);
	assert(restore_route(&graph, &state, path) == 0);
	mock = mock_state();
	assert(mock->operation_count == 0U);
	assert(access(path, F_OK) != 0);
}

static void test_graph_mismatch_retains_state(void)
{
	RouteGraph graph;
	RouteState state;
	char path[PATH_MAX];
	struct mock_device *mock;

	init_state(&state, false, false);
	make_state_file(path, sizeof(path), &state);
	init_graph(&graph, true, true);
	graph.links[1].sink_pad++;
	reset_mock(&graph, 0U);
	assert(restore_route(&graph, &state, path) != 0);
	mock = mock_state();
	assert(mock->operation_count == 0U);
	assert(access(path, F_OK) == 0);
	unlink(path);
}

static void test_saved_enabled_link_disabled_externally(void)
{
	RouteGraph graph;
	RouteState state;
	char path[PATH_MAX];
	struct mock_device *mock;

	init_state(&state, true, false);
	make_state_file(path, sizeof(path), &state);
	init_graph(&graph, false, true);
	reset_mock(&graph, 0U);
	assert(restore_route(&graph, &state, path) != 0);
	mock = mock_state();
	assert(mock->operation_count == 0U);
	assert(access(path, F_OK) == 0);
	unlink(path);
}

int main(void)
{
	test_partial_prepare_failure();
	test_partial_prepare_retains_state();
	test_partial_restore_failure_and_retry();
	test_already_restored_links();
	test_graph_mismatch_retains_state();
	test_saved_enabled_link_disabled_externally();
	puts("ir-route-mock-test: PASS");
	return 0;
}
