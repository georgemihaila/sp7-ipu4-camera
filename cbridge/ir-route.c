#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <linux/media.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

#define IR_ROUTE_MAX_ENTITIES 128U
#define IR_ROUTE_MAX_LINKS 512U
#define IR_ROUTE_STATE_VERSION 1U
#define IR_ROUTE_DEFAULT_STATE "/run/sp7-camera-ir-route.state"
#define IR_ROUTE_NAME_SIZE 32U

typedef struct {
	char source[IR_ROUTE_NAME_SIZE];
	unsigned source_pad;
	char sink[IR_ROUTE_NAME_SIZE];
	unsigned sink_pad;
	bool enabled;
} RouteLink;

typedef struct {
	char media[PATH_MAX];
	RouteLink links[2];
} RouteState;

typedef struct {
	int fd;
	char path[PATH_MAX];
	struct media_entity_desc entities[IR_ROUTE_MAX_ENTITIES];
	unsigned entity_count;
	RouteLink links[2];
	uint32_t source_ids[2];
	uint32_t sink_ids[2];
	unsigned source_pads[2];
	unsigned sink_pads[2];
	uint32_t flags[2];
} RouteGraph;

static void usage(FILE *stream, const char *program)
{
	fprintf(stream,
		"Usage: %s prepare|restore [--media PATH] [--state PATH]\n"
		"\n"
		"Discover the OV7251 source-6 media route by entity name. prepare\n"
		"snapshots the two target link states and enables them. restore\n"
		"returns those links to the saved states. Authentication never calls\n"
		"this tool.\n",
		program);
}

static int failf(const char *format, ...)
{
	va_list args;

	va_start(args, format);
	fputs("ir-route: error: ", stderr);
	vfprintf(stderr, format, args);
	fputc('\n', stderr);
	va_end(args);
	return 2;
}

static bool name_is(const char *actual, const char *expected)
{
	return actual != NULL && expected != NULL && strcmp(actual, expected) == 0;
}

static bool name_has(const char *actual, const char *needle)
{
	return actual != NULL && needle != NULL && strstr(actual, needle) != NULL;
}

static int enumerate_entities(RouteGraph *graph)
{
	uint32_t next = MEDIA_ENT_ID_FLAG_NEXT;

	graph->entity_count = 0U;
	while (graph->entity_count < IR_ROUTE_MAX_ENTITIES) {
		struct media_entity_desc *entity =
			&graph->entities[graph->entity_count];

		memset(entity, 0, sizeof(*entity));
		entity->id = next;
		if (ioctl(graph->fd, MEDIA_IOC_ENUM_ENTITIES, entity) < 0) {
			if (errno == EINVAL)
				return 0;
			return failf("MEDIA_IOC_ENUM_ENTITIES on %s: %s",
				graph->path, strerror(errno));
		}
		next = entity->id | MEDIA_ENT_ID_FLAG_NEXT;
		graph->entity_count++;
	}
	return failf("media graph on %s has too many entities", graph->path);
}

static int find_entity(const RouteGraph *graph, const char *name,
	bool substring)
{
	for (unsigned index = 0U; index < graph->entity_count; index++) {
		const char *actual = graph->entities[index].name;

		if ((substring && name_has(actual, name)) ||
			(!substring && name_is(actual, name)))
			return (int)index;
	}
	return -1;
}

static int find_link_for_pair(RouteGraph *graph, unsigned link_index,
	uint32_t source_id, uint32_t sink_id)
{
	for (unsigned entity_index = 0U;
		 entity_index < graph->entity_count; entity_index++) {
		const struct media_entity_desc *entity =
			&graph->entities[entity_index];
		struct media_pad_desc *pads = NULL;
		struct media_link_desc *links = NULL;
		struct media_links_enum enumeration = { 0 };
		int result = 0;

		if (entity->links > IR_ROUTE_MAX_LINKS)
			return failf("entity %s has too many links", entity->name);
		pads = calloc(entity->pads == 0U ? 1U : entity->pads, sizeof(*pads));
		links = calloc(entity->links == 0U ? 1U : entity->links,
			sizeof(*links));
		if (pads == NULL || links == NULL) {
			free(pads);
			free(links);
			return failf("out of memory enumerating %s links", entity->name);
		}
		enumeration.entity = entity->id;
		enumeration.pads = pads;
		enumeration.links = links;
		if (ioctl(graph->fd, MEDIA_IOC_ENUM_LINKS, &enumeration) < 0) {
			result = failf("MEDIA_IOC_ENUM_LINKS for %s: %s", entity->name,
				strerror(errno));
			free(pads);
			free(links);
			return result;
		}
		for (unsigned index = 0U; index < entity->links; index++) {
			const struct media_link_desc *link = &links[index];

			if (link->source.entity != source_id ||
				link->sink.entity != sink_id)
				continue;
			graph->source_ids[link_index] = link->source.entity;
			graph->sink_ids[link_index] = link->sink.entity;
			graph->source_pads[link_index] = link->source.index;
			graph->sink_pads[link_index] = link->sink.index;
			graph->flags[link_index] = link->flags;
			result = 1;
		}
		free(pads);
		free(links);
		if (result == 1)
			return 1;
		if (result != 0)
			return result;
	}
	return 0;
}

static int fill_route_links(RouteGraph *graph)
{
	int sensor = find_entity(graph, "ov7251", true);
	int csi = find_entity(graph, "Intel IPU4 CSI-2 1", false);
	int capture = find_entity(graph, "Intel IPU4 CSI-2 1 capture 0", false);
	int result;

	if (sensor < 0 || csi < 0 || capture < 0)
		return 1;
	result = find_link_for_pair(graph, 0U, graph->entities[sensor].id,
		graph->entities[csi].id);
	if (result != 1)
		return result == 0 ? 1 : result;
	result = find_link_for_pair(graph, 1U, graph->entities[csi].id,
		graph->entities[capture].id);
	if (result != 1)
		return result == 0 ? 1 : result;

	snprintf(graph->links[0].source, sizeof(graph->links[0].source), "%s",
		graph->entities[sensor].name);
	graph->links[0].source_pad = graph->source_pads[0];
	snprintf(graph->links[0].sink, sizeof(graph->links[0].sink), "%s",
		graph->entities[csi].name);
	graph->links[0].sink_pad = graph->sink_pads[0];
	graph->links[0].enabled =
		(graph->flags[0] & MEDIA_LNK_FL_ENABLED) != 0U;
	snprintf(graph->links[1].source, sizeof(graph->links[1].source), "%s",
		graph->entities[csi].name);
	graph->links[1].source_pad = graph->source_pads[1];
	snprintf(graph->links[1].sink, sizeof(graph->links[1].sink), "%s",
		graph->entities[capture].name);
	graph->links[1].sink_pad = graph->sink_pads[1];
	graph->links[1].enabled =
		(graph->flags[1] & MEDIA_LNK_FL_ENABLED) != 0U;
	return 0;
}

static int open_graph(RouteGraph *graph, const char *path)
{
	int result;

	memset(graph, 0, sizeof(*graph));
	graph->fd = open(path, O_RDWR | O_CLOEXEC);
	if (graph->fd < 0)
		return 1;
	if (snprintf(graph->path, sizeof(graph->path), "%s", path) >=
		(int)sizeof(graph->path)) {
		close(graph->fd);
		return 1;
	}
	result = enumerate_entities(graph);
	if (result != 0) {
		close(graph->fd);
		return result;
	}
	result = fill_route_links(graph);
	if (result != 0) {
		close(graph->fd);
		return result;
	}
	return 0;
}

static int graph_discover(RouteGraph *graph, const char *requested_media)
{
	glob_t matches = { 0 };
	int result;

	if (requested_media != NULL) {
		result = open_graph(graph, requested_media);
		if (result == 0)
			return 0;
		if (result == 1)
			return failf("the OV7251 source-6 route was not found in %s",
				requested_media);
		return result;
	}
	if (glob("/dev/media*", 0, NULL, &matches) != 0)
		return failf("no media-controller nodes exist");
	for (size_t index = 0U; index < matches.gl_pathc; index++) {
		result = open_graph(graph, matches.gl_pathv[index]);
		if (result == 0) {
			globfree(&matches);
			return 0;
		}
		if (result != 1) {
			globfree(&matches);
			return result;
		}
	}
	globfree(&matches);
	return failf("the OV7251 source-6 route was not found in any media graph");
}

static int setup_link(RouteGraph *graph, unsigned index, bool enabled)
{
	struct media_link_desc link = { 0 };
	int result;

	link.source.entity = graph->source_ids[index];
	link.source.index = (__u16)graph->source_pads[index];
	link.sink.entity = graph->sink_ids[index];
	link.sink.index = (__u16)graph->sink_pads[index];
	link.flags = enabled ? MEDIA_LNK_FL_ENABLED : 0U;
	if (ioctl(graph->fd, MEDIA_IOC_SETUP_LINK, &link) < 0)
		return failf("set %s:%u -> %s:%u %s: %s",
			graph->links[index].source, graph->links[index].source_pad,
			graph->links[index].sink, graph->links[index].sink_pad,
			enabled ? "on" : "off", strerror(errno));
	result = find_link_for_pair(graph, index, graph->source_ids[index],
		graph->sink_ids[index]);
	if (result != 1)
		return result == 0 ? failf("link verification found no %s:%u -> %s:%u",
			graph->links[index].source, graph->links[index].source_pad,
			graph->links[index].sink, graph->links[index].sink_pad) : result;
	if (((graph->flags[index] & MEDIA_LNK_FL_ENABLED) != 0U) != enabled)
		return failf("link verification disagreed for %s:%u -> %s:%u",
			graph->links[index].source, graph->links[index].source_pad,
			graph->links[index].sink, graph->links[index].sink_pad);
	return 0;
}

static int write_state(const char *path, const RouteGraph *graph)
{
	char temporary[PATH_MAX];
	FILE *stream;
	int fd;

	if (snprintf(temporary, sizeof(temporary), "%s.tmp.%ld", path,
		(long)getpid()) >= (int)sizeof(temporary))
		return failf("state path is too long");
	fd = open(temporary, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
		0600);
	if (fd < 0)
		return failf("create state file %s: %s", temporary, strerror(errno));
	stream = fdopen(fd, "w");
	if (stream == NULL) {
		close(fd);
		unlink(temporary);
		return failf("open state file stream %s: %s", temporary, strerror(errno));
	}
	fprintf(stream, "version=%u\nmedia=%s\n", IR_ROUTE_STATE_VERSION,
		graph->path);
	for (unsigned index = 0U; index < 2U; index++) {
		fprintf(stream, "link%u_source=%s\nlink%u_source_pad=%u\n"
			"link%u_sink=%s\nlink%u_sink_pad=%u\nlink%u_enabled=%u\n",
			index, graph->links[index].source, index,
			graph->links[index].source_pad, index, graph->links[index].sink,
			index, graph->links[index].sink_pad, index,
			graph->links[index].enabled ? 1U : 0U);
	}
	{
		int write_result = fflush(stream);

		if (write_result == 0)
			write_result = fsync(fileno(stream));
		if (fclose(stream) != 0)
			write_result = -1;
		if (write_result != 0) {
			int saved_errno = errno;

			unlink(temporary);
			return failf("commit state file %s: %s", path,
				strerror(saved_errno));
		}
	}
	if (rename(temporary, path) != 0) {
		int saved_errno = errno;

		unlink(temporary);
		return failf("commit state file %s: %s", path, strerror(saved_errno));
	}
	return 0;
}

static int read_value(const char *path, const char *key, char *value,
	size_t value_size)
{
	FILE *stream = fopen(path, "r");
	char line[PATH_MAX + 64];
	const size_t key_size = strlen(key);
	int found = 0;

	if (stream == NULL)
		return failf("open state file %s: %s", path, strerror(errno));
	while (fgets(line, sizeof(line), stream) != NULL) {
		if (strncmp(line, key, key_size) != 0 || line[key_size] != '=')
			continue;
		char *end = strpbrk(line + key_size + 1U, "\r\n");

		if (end != NULL)
			*end = '\0';
		if (snprintf(value, value_size, "%s", line + key_size + 1U) >=
			(int)value_size) {
			fclose(stream);
			return failf("state value %s is too long", key);
		}
		found = 1;
		break;
	}
	fclose(stream);
	return found ? 0 : failf("state file %s is missing %s", path, key);
}

static int read_unsigned(const char *path, const char *key, unsigned *value)
{
	char text[32];
	char *end;
	unsigned long parsed;
	int result = read_value(path, key, text, sizeof(text));

	if (result != 0)
		return result;
	errno = 0;
	parsed = strtoul(text, &end, 10);
	if (errno != 0 || end == text || *end != '\0' || parsed > UINT32_MAX)
		return failf("state value %s is invalid", key);
	*value = (unsigned)parsed;
	return 0;
}

static int read_state(const char *path, RouteState *state)
{
	int result;
	unsigned version;

	memset(state, 0, sizeof(*state));
	result = read_unsigned(path, "version", &version);
	if (result != 0)
		return result;
	if (version != IR_ROUTE_STATE_VERSION)
		return failf("unsupported route state version in %s", path);
	if (read_value(path, "media", state->media, sizeof(state->media)) != 0)
		return 2;
	for (unsigned index = 0U; index < 2U; index++) {
		char key[64];
		RouteLink *link = &state->links[index];

		snprintf(key, sizeof(key), "link%u_source", index);
		if (read_value(path, key, link->source, sizeof(link->source)) != 0)
			return 2;
		snprintf(key, sizeof(key), "link%u_source_pad", index);
		if (read_unsigned(path, key, &link->source_pad) != 0)
			return 2;
		snprintf(key, sizeof(key), "link%u_sink", index);
		if (read_value(path, key, link->sink, sizeof(link->sink)) != 0)
			return 2;
		snprintf(key, sizeof(key), "link%u_sink_pad", index);
		if (read_unsigned(path, key, &link->sink_pad) != 0)
			return 2;
		snprintf(key, sizeof(key), "link%u_enabled", index);
		{
			unsigned enabled;

			if (read_unsigned(path, key, &enabled) != 0 || enabled > 1U)
				return failf("state link%u enabled value is invalid", index);
			link->enabled = enabled != 0U;
		}
	}
	return 0;
}

static int restore_link(RouteGraph *graph, const RouteLink *saved,
	unsigned index)
{
	int source = find_entity(graph, saved->source, false);
	int sink = find_entity(graph, saved->sink, false);

	if (source < 0 || sink < 0)
		return failf("saved route entities are absent from %s", graph->path);
	graph->source_ids[index] = graph->entities[source].id;
	graph->sink_ids[index] = graph->entities[sink].id;
	graph->source_pads[index] = saved->source_pad;
	graph->sink_pads[index] = saved->sink_pad;
	graph->links[index] = *saved;
	return setup_link(graph, index, saved->enabled);
}

static int parse_options(int argc, char **argv, const char **media,
	const char **state)
{
	for (int index = 2; index < argc; index++) {
		if (strcmp(argv[index], "--media") == 0 && index + 1 < argc) {
			*media = argv[++index];
		} else if (strcmp(argv[index], "--state") == 0 && index + 1 < argc) {
			*state = argv[++index];
		} else if (strcmp(argv[index], "--help") == 0) {
			return 1;
		} else {
			return failf("unknown or incomplete option: %s", argv[index]);
		}
	}
	return 0;
}

int main(int argc, char **argv)
{
	const char *media = NULL;
	const char *state_path = IR_ROUTE_DEFAULT_STATE;
	RouteGraph graph;
	RouteState state;
	int result;

	if (argc < 2 || strcmp(argv[1], "--help") == 0) {
		usage(argc < 1 ? stderr : stdout, argv[0]);
		return argc < 2 ? 2 : 0;
	}
	if (strcmp(argv[1], "prepare") != 0 && strcmp(argv[1], "restore") != 0)
		return failf("command must be prepare or restore");
	result = parse_options(argc, argv, &media, &state_path);
	if (result != 0)
		return result == 1 ? (usage(stdout, argv[0]), 0) : result;

	if (strcmp(argv[1], "prepare") == 0) {
		if (access(state_path, F_OK) == 0)
			return failf("state file already exists: %s; restore it first",
				state_path);
		if (errno != ENOENT)
			return failf("cannot inspect state file %s: %s", state_path,
				strerror(errno));
		result = graph_discover(&graph, media);
		if (result != 0)
			return result;
		if (write_state(state_path, &graph) != 0) {
			close(graph.fd);
			return 2;
		}
		for (unsigned index = 0U; index < 2U; index++) {
			if (graph.links[index].enabled)
				continue;
			if (setup_link(&graph, index, true) != 0) {
				int restore_result = 0;

				for (unsigned reverse = index; reverse > 0U; reverse--)
					if (setup_link(&graph, reverse - 1U,
						graph.links[reverse - 1U].enabled) != 0)
						restore_result = 2;
				close(graph.fd);
				if (restore_result == 0)
					unlink(state_path);
				return 2;
			}
		}
		close(graph.fd);
		printf("ir-route prepared media=%s state=%s\n", graph.path, state_path);
		return 0;
	}

	if (read_state(state_path, &state) != 0)
		return 2;
	result = graph_discover(&graph, media != NULL ? media : state.media);
	if (result != 0)
		return result;
	/* Restore in reverse pipeline order so the capture sink is released first. */
	for (unsigned reverse = 2U; reverse > 0U; reverse--) {
		unsigned index = reverse - 1U;

		if (restore_link(&graph, &state.links[index], index) != 0) {
			close(graph.fd);
			return 2;
		}
	}
	close(graph.fd);
	if (unlink(state_path) != 0)
		return failf("restored route but could not remove state %s: %s",
			state_path, strerror(errno));
	printf("ir-route restored state=%s\n", state_path);
	return 0;
}
