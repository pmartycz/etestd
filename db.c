#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <json-c/json.h>
#include <uuid/uuid.h>

#include "common.h"
#include "db.h"

#define TESTS_FILENAME      "tests"
#define ANSWERS_FILENAME    "answers"
#define USERS_FILENAME      "users"
#define GROUPS_FILENAME     "groups"

#define JSON_FLAGS          (JSON_C_TO_STRING_PRETTY | JSON_C_TO_STRING_SPACED)

static FILE *tests_file;
static FILE *answers_file;
static FILE *users_file;
static FILE *groups_file;

int open_db(const char *db_dir)
{
    int ret = 0;
    char *tests_filename, *answers_filename,
         *users_filename, *groups_filename;

    /* no error checking */
    asprintf(&tests_filename, "%s/%s", db_dir, TESTS_FILENAME);
    asprintf(&answers_filename, "%s/%s", db_dir, ANSWERS_FILENAME);
    asprintf(&users_filename, "%s/%s", db_dir, USERS_FILENAME);
    asprintf(&groups_filename, "%s/%s", db_dir, GROUPS_FILENAME);

    tests_file = fopen(tests_filename, "r+");
    if (!tests_file) {
        log_errno(tests_filename);
        goto err_tests;
    }
    answers_file = fopen(answers_filename, "r+");
    if (!answers_file) {
        log_errno(answers_filename);
        goto err_answers;
    }
    users_file = fopen(users_filename, "r+");
    if (!users_file) {
        log_errno(users_filename);
        goto err_users;
    }
    groups_file = fopen(groups_filename, "r+");
    if (!groups_file) {
        log_errno(groups_filename);
        goto err_groups;
    }

    goto out;

/* stack unwind cleanup */
err_groups:
    fclose(users_file);
err_users:
    fclose(answers_file);
err_answers:
    fclose(tests_file);
err_tests:
    ret = -1;
out:
    free(tests_filename);
    free(answers_filename);
    free(users_filename);
    free(groups_filename);

    return ret;
}

void close_db(void)
{
    fclose(tests_file);
    fclose(answers_file);
    fclose(users_file);
    fclose(groups_file);
}
 
static char *file_to_string(FILE *fp)
{
    /* Go to the end of the file. */
    if (fseek(fp, 0L, SEEK_END) != 0) {
        log_errno("fseek");
        return NULL;
    }
        
    /* Get the size of the file. */
    long bufsize = ftell(fp);
    if (bufsize == -1) {
        log_errno("ftell");
        return NULL;
    }

    /* Allocate our buffer to that size. */
    char *buf = malloc(bufsize + 1);
    if (!buf) {
        log_errno("malloc");
        return NULL;
    }

    /* Go back to the start of the file. Read the entire file into memory. */
    if (fseek(fp, 0L, SEEK_SET) != 0 || fread(buf, 1, bufsize, fp) != bufsize) {
        free(buf);
        return NULL;
    }
    
    buf[bufsize] = '\0';
    return buf;
}

static json_object *get_json_from_file(FILE *fp)
{
    char *json_string = file_to_string(fp);
    if (!json_string)
        return NULL;

    json_object *obj = json_tokener_parse(json_string);

    //fprintf(stderr, "%s\n\n", json_object_to_json_string_ext(obj, JSON_C_TO_STRING_PRETTY));

    free(json_string);
    
    return obj;
}

json_object *get_users(void)
{
    return get_json_from_file(users_file);
}

json_object *get_groups(void)
{
    return get_json_from_file(groups_file);
}

json_object *get_tests(void)
{
    return get_json_from_file(tests_file);
}

json_object *get_answers(void)
{
    json_object *answers = get_json_from_file(answers_file);

    if (!answers)
        return json_object_new_array();
    return answers;
}

static void put_json_to_file(json_object *obj, FILE *fp)
{
    fseek(fp, 0, SEEK_SET);
    fputs(json_object_to_json_string_ext(obj, JSON_FLAGS), fp);
    fflush(fp);
    ftruncate(fileno(fp), ftello(fp));
}

void put_answers(json_object *answers)
{
    put_json_to_file(answers, answers_file);
}

static int key_value_equals_str(json_object *obj, const char *key, const char *str)
{
    json_object *value;
    if (json_object_object_get_ex(obj, key, &value) == TRUE)
        if (json_object_is_type(value, json_type_string))
            if (strcmp(json_object_get_string(value), str) == 0)
                return 1;
    return 0;
}

static int key_value_equals_uuid(json_object *obj, const char *key, uuid_t uuid)
{
    json_object *value;
    if (json_object_object_get_ex(obj, key, &value) == TRUE)
        if (json_object_is_type(value, json_type_string)) {
            uuid_t uuid_in_obj;
            if (uuid_parse(json_object_get_string(value), uuid_in_obj) == 0)
                if (uuid_compare(uuid, uuid_in_obj) == 0)
                    return 1;
        }
    return 0;
}

static int key_value_is_null(json_object *obj, const char *key)
{
    json_object *value;
    if (json_object_object_get_ex(obj, key, &value) == TRUE)
        if (json_object_is_type(value, json_type_null))
            return 1;
    return 0;
}

json_object *get_test(uuid_t id, json_object *tests)
{
    if (!json_object_is_type(tests, json_type_array))
        return NULL;
        
    for (int i = 0; i < json_object_array_length(tests); i++) {
        json_object *test = json_object_array_get_idx(tests, i);
        if (key_value_equals_uuid(test, "id", id))
            return test;
    }

    return NULL;
}

static json_object *get_test_answers_record(uuid_t test_id, json_object *answers)
{
    if (!json_object_is_type(answers, json_type_array))
        return NULL;
    
    for (int i = 0; i < json_object_array_length(answers); i++) {
        json_object *test_record = json_object_array_get_idx(answers, i);
        if (key_value_equals_uuid(test_record, "testId", test_id))
            return test_record;
    }

    return NULL;
}

static json_object *create_test_answers_record(uuid_t test_id, json_object *answers)
{
    if (!json_object_is_type(answers, json_type_array))
        return NULL;

    json_object *test_record = json_object_new_object();
    
    char s[37];
    uuid_unparse(test_id, s);
    
    json_object_object_add(test_record, "testId", json_object_new_string(s));
    json_object_object_add(test_record, "subjects", json_object_new_array());
    
    json_object_array_add(answers, test_record);
    
    return test_record;
}

static json_object *get_user_answers_record(uuid_t test_id, const char *username, json_object *answers)
{
    json_object *test_record = get_test_answers_record(test_id, answers);

    json_object *test_subjects;
    if (json_object_object_get_ex(test_record, "subjects", &test_subjects) != TRUE)
        return NULL;
    
    if (!json_object_is_type(test_subjects, json_type_array))
        return NULL;
        
    for (int i = 0; i < json_object_array_length(test_subjects); i++) {
        json_object *record = json_object_array_get_idx(test_subjects, i);
        if (key_value_equals_str(record, "name", username)) {
            return record;
        }
    }

    return NULL;
}

static json_object *create_user_answers_record(uuid_t test_id, const char *username, json_object *answers)
{
    json_object *test_record = get_test_answers_record(test_id, answers);

    if (!test_record)
        test_record = create_test_answers_record(test_id, answers);

    json_object *test_subjects;
    if (json_object_object_get_ex(test_record, "subjects", &test_subjects) != TRUE)
        return NULL;

    if (!json_object_is_type(test_subjects, json_type_array))
        return NULL;

    json_object *user_record = json_object_new_object();
    json_object_object_add(user_record, "name", json_object_new_string(username));
    json_object_object_add(user_record, "answers", NULL);
    json_object_object_add(user_record, "creationTime", json_object_new_int64(time(NULL)));

    json_object_array_add(test_subjects, user_record);

    return user_record;
}

json_object *get_test_for_student(uuid_t id, const char *username, json_object *tests)
{
    json_object *test = get_test(id, tests);

    if (!json_object_is_type(test, json_type_object))
        return NULL;
    
    /* If test isn't available yet return nothing */
    json_object *start_time;
    if (json_object_object_get_ex(test, "startTime", &start_time) == TRUE) {
        int64_t now = time(NULL);
        if (json_object_is_type(start_time, json_type_int))
            if (json_object_get_int64(start_time) > now)
                return NULL;
    }
    
    /* If results haven't been made available by examinator
     * remove correct answers */
    json_object *results_available;
    if (json_object_object_get_ex(test, "resultsAvailable", &results_available) == TRUE)
        if (json_object_is_type(results_available, json_type_boolean))
            if(json_object_get_boolean(results_available) == FALSE)
                json_object_object_del(test, "correctAnswers");

    json_object *answers = get_answers();
    json_object *user_record = get_user_answers_record(id, username, answers);
    
    /* If there is no answer record (student gets test for the first time)
     * create an empty one with current time logged */
    if (!user_record) {
        create_user_answers_record(id, username, answers);
        put_answers(answers);
    /* Else if user has submitted answers add them to test */    
    } else {
        json_object *user_answers;
        if (json_object_object_get_ex(user_record, "answers", &user_answers) == TRUE)
            if (!json_object_is_type(user_answers, json_type_null))
                json_object_object_add(test, "userAnswers", json_object_get(user_answers));
    }

    json_object_put(answers);
    return test;
}

static json_object *get_entity(const char *name, json_object *entities)
{
    if (!json_object_is_type(entities, json_type_array))
        return NULL;
        
    for (int i = 0; i < json_object_array_length(entities); i++) {
        json_object *entity = json_object_array_get_idx(entities, i);
        json_object *value;
        if (json_object_object_get_ex(entity, "name", &value) == TRUE)
            if (strcmp(json_object_get_string(value), name) == 0)
                    return entity;
    }

    return NULL;
}

int user_is_group_member(const char *username, const char *groupname, json_object *groups)
{
    json_object *group = get_entity(groupname, groups);

    json_object *members;
    if (json_object_object_get_ex(group, "members", &members) == TRUE) {
        if (!json_object_is_type(members, json_type_array))
            return 0;
        for (int i = 0; i < json_object_array_length(members); i++) {
            json_object *member = json_object_array_get_idx(members, i);
            if (strcmp(json_object_get_string(member), username) == 0)
                return 1;
        }
    }
    return 0;
}

int user_is_administrator(const char *username, json_object *groups)
{
    return user_is_group_member(username, "administrators", groups);
}

int user_is_examiner(const char *username, json_object *groups)
{
    return user_is_group_member(username, "examiners", groups);
}

int entity_exists(const char *name, json_object *obj)
{
    return get_entity(name, obj) ? 1 : 0;
}

json_object *remove_qa_from_tests(json_object *tests)
{
    if (!json_object_is_type(tests, json_type_array))
        return NULL;
        
    for (int i = 0; i < json_object_array_length(tests); i++) {
        json_object *test = json_object_array_get_idx(tests, i);
        if (!json_object_is_type(test, json_type_object))
            continue;
        json_object_object_del(test, "questions");
        json_object_object_del(test, "correctAnswers");
    }

    return tests;
}

/* allocates memory! */
json_object *get_tests_for_examiner(const char *username)
{
    json_object *tests = get_tests();
    json_object *examiner_tests = json_object_new_array();

    if (!json_object_is_type(tests, json_type_array))
        goto cleanup;

    for (int i = 0; i < json_object_array_length(tests); i++) {
        json_object *test = json_object_array_get_idx(tests, i);
        json_object *owner;
        if (json_object_object_get_ex(test, "owner", &owner) == TRUE) {
            if (!json_object_is_type(owner, json_type_string))
                continue;
            if (strcmp(json_object_get_string(owner), username) == 0)
                json_object_array_add(examiner_tests, json_object_get(test));
        }
        
    }

cleanup:
    json_object_put(tests);
    
    return examiner_tests;
}

/* allocates memory! */
json_object *get_tests_for_student(const char *username)
{
    json_object *tests = get_tests();
    json_object *groups = get_groups();
    json_object *student_tests = json_object_new_array();

    if (!json_object_is_type(tests, json_type_array))
        goto cleanup;

    /* Student needs to be a member of one of the groups
     * specified in the test to receive it */
    for (int i = 0; i < json_object_array_length(tests); i++) {
        json_object *test = json_object_array_get_idx(tests, i);
        json_object *test_groups;
        if (json_object_object_get_ex(test, "groups", &test_groups) == TRUE) {
            if (!json_object_is_type(test_groups, json_type_array))
                continue;
            for (int i = 0; i < json_object_array_length(test_groups); i++) {
                json_object *test_group = json_object_array_get_idx(test_groups, i);
                if (!json_object_is_type(test_group, json_type_string))
                    continue;
                if (user_is_group_member(username, json_object_get_string(test_group), groups)) {
                    json_object_array_add(student_tests, json_object_get(test));
                    break;
                }
            }
        }
    }
     
cleanup:
    json_object_put(groups);
    json_object_put(tests);
    
    return student_tests;
}

int answers_to_test_are_valid(json_object *test, json_object *submitted_answers)
{
    json_object *test_type;
    json_object *questions;
    int len;
    
    if (json_object_object_get_ex(test, "type", &test_type) != TRUE ||
        json_object_object_get_ex(test, "questions", &questions) != TRUE ||
        !json_object_is_type(test_type, json_type_string) ||
        !json_object_is_type(questions, json_type_array) ||
        !json_object_is_type(submitted_answers, json_type_array) ||
        (len = json_object_array_length(submitted_answers)) != json_object_array_length(questions))
        return 0;
        
    if (strcmp(json_object_get_string(test_type), "single") == 0) {
        for (int i = 0; i < len; i++) {
            json_object *question = json_object_array_get_idx(questions, i);
            json_object *submitted_answer = json_object_array_get_idx(submitted_answers, i);
            json_object *options;
            if (json_object_object_get_ex(question, "options", &options) != TRUE ||
                !json_object_is_type(options, json_type_array) ||
                !json_object_is_type(submitted_answer, json_type_int) ||
                json_object_get_int64(submitted_answers) < 0 ||
                json_object_get_int64(submitted_answers) >= json_object_array_length(options)) {
                    
                return 0;
            }
        }
        return 1;
    } else if (strcmp(json_object_get_string(test_type), "multi") == 0) {
        /* TODO validate type multi answers */
        return 1;
    } else
        return 0;
}

int submit_answers(uuid_t id, const char *username, json_object *submitted_answers)
{
    json_object *tests = get_tests();
    json_object *test = get_test(id, tests);
    json_object *answers = get_answers();
    json_object *user_answers_record = get_user_answers_record(id, username, answers);
    int retval = -1;

    if (key_value_is_null(user_answers_record, "answers")) {
        json_object *creation_time;
        json_object *time_limit;
        if (json_object_object_get_ex(user_answers_record, "creationTime", &creation_time) == TRUE &&
            json_object_object_get_ex(test, "timeLimit", &time_limit) == TRUE &&
            json_object_is_type(creation_time, json_type_int) &&
            json_object_is_type(time_limit, json_type_int)) {
                
            int64_t now = time(NULL);
            if (now < json_object_get_int64(creation_time) + json_object_get_int64(time_limit) * 60)
                if (answers_to_test_are_valid(test, submitted_answers)) {
                    json_object_object_add(user_answers_record, "answers", submitted_answers);
                    put_answers(answers);
                    retval = 0;
                }
        }
    }

    json_object_put(tests);
    json_object_put(answers);
    return retval;
}
