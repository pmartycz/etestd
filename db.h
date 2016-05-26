#include <json-c/json.h>
#include <uuid/uuid.h>

struct json_object *get_test(uuid_t id, struct json_object *tests);

int write_test_headers(struct json_object *tests, FILE *fp);
