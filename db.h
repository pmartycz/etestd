#include <json-c/json.h>
#include <uuid/uuid.h>

int open_db(const char *db_dir);
void close_db(void);

int entity_exists(const char *name, json_object *obj);
int user_is_examiner(const char *username, json_object *groups);
int user_is_administrator(const char *username, json_object *groups);
int user_is_group_member(const char *username, const char *groupname, json_object *groups);

json_object *get_tests(void);
json_object *get_tests_for_student(const char *username);
json_object *get_tests_for_examiner(const char *username);
json_object *get_answers(void);
json_object *get_users(void);
json_object *get_groups(void);

json_object *remove_qa_from_tests(json_object *tests);

json_object *get_test(uuid_t id, json_object *tests);
json_object *get_test_for_student(uuid_t id, const char *username, json_object *tests);

int submit_answers(uuid_t id, const char *username, json_object *submitted_answers);
