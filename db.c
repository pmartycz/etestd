/**
 * @file
 * @author Piotr Martycz <pmartycz@gmail.com>
 *
 * @section DESCRIPTION
 * Module for database access and JSON manipulation.
 */
 
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <json-c/json.h>
#include <uuid/uuid.h>
#include <stdbool.h>

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

/**
 * Open database files
 *
 * @param db_dir directory where database is located
 *
 * @return 0 on success
 */
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

/**
 * Close database files
 */
void close_db(void)
{
    fclose(tests_file);
    fclose(answers_file);
    fclose(users_file);
    fclose(groups_file);
}

/**
 * Read contents of a file to dynamically
 * allocated string
 *
 * @param fp stream opened for reading 
 *
 * @return Pointer to buffer with file contents
 * or NULL on error.
 */
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


/**
 * Get JSON object from file
 *
 * Parses file and creates a json_object from
 * its contents.
 *
 * @param file stream
 * @return pointer to json_object
 */
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

/**
 * Get json_object containing users database
 */
json_object *get_users(void)
{
    return get_json_from_file(users_file);
}

/**
 * Get json_object containing groups database
 */
json_object *get_groups(void)
{
    return get_json_from_file(groups_file);
}

/**
 * Get json_object containing tests database
 */
json_object *get_tests(void)
{
    return get_json_from_file(tests_file);
}

/**
 * Get json_object containing answers database
 */
json_object *get_answers(void)
{
    json_object *answers = get_json_from_file(answers_file);

    if (!answers)
        return json_object_new_array();
    return answers;
}

/**
 * Write JSON data to file
 *
 * @param obj JSON data
 * @param file stream opened for writing
 */
static void put_json_to_file(json_object *obj, FILE *fp)
{
    fseek(fp, 0, SEEK_SET);
    fputs(json_object_to_json_string_ext(obj, JSON_FLAGS), fp);
    fflush(fp);
    ftruncate(fileno(fp), ftello(fp));

    if (ferror(fp))
        log_msg_die("Database write error");
}

/**
 * Write json_object to answers database
 */
void put_answers(json_object *answers)
{
    put_json_to_file(answers, answers_file);
}

/**
 * Write json_object to tests database
 */
void put_tests(json_object *tests)
{
    put_json_to_file(tests, tests_file);
}

/**
 * Write json_object to groups database
 */
void put_groups(json_object *groups)
{
    put_json_to_file(groups, groups_file);
}

/**
 * Check if JSON object key has value equal to str
 * 
 * @param obj json_object of type json_type_object
 */
static int key_value_equals_str(json_object *obj, const char *key, const char *str)
{
    json_object *value;
    if (json_object_object_get_ex(obj, key, &value) == TRUE)
        if (json_object_is_type(value, json_type_string))
            if (strcmp(json_object_get_string(value), str) == 0)
                return 1;
    return 0;
}


/**
 * Check if JSON object key value equals uuid
 * 
 * @param obj json_object of type json_type_object
 */
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

/**
 * Check if JSON object key value is of type null
 * 
 * @param obj json_object of type json_type_object
 */
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

static int64_t create_user_answers_record(uuid_t test_id, const char *username, json_object *answers)
{
    json_object *test_record = get_test_answers_record(test_id, answers);

    if (!test_record)
        test_record = create_test_answers_record(test_id, answers);

    json_object *test_subjects;
    if (json_object_object_get_ex(test_record, "subjects", &test_subjects) != TRUE)
        return 0;

    if (!json_object_is_type(test_subjects, json_type_array))
        return 0;

    json_object *user_record = json_object_new_object();
    json_object_object_add(user_record, "name", json_object_new_string(username));
    json_object_object_add(user_record, "answers", NULL);
    int64_t now = time(NULL);
    json_object_object_add(user_record, "creationTime", json_object_new_int64(now));

    json_object_array_add(test_subjects, user_record);

    return now;
}

json_object *get_test_for_student(uuid_t id, const char *username, json_object *tests)
{
    json_object *test = get_test(id, tests);

    if (!json_object_is_type(test, json_type_object))
        return NULL;
    
    json_object *start_time;
    json_object *end_time;
    json_object *results_available;

    if (json_object_object_get_ex(test, "startTime", &start_time) != TRUE ||
        !json_object_is_type(start_time, json_type_int) ||
        json_object_object_get_ex(test, "endTime", &end_time) != TRUE ||
        !json_object_is_type(end_time, json_type_int) ||
        json_object_object_get_ex(test, "resultsAvailable", &results_available) != TRUE ||
        !json_object_is_type(results_available, json_type_boolean))
        return NULL;
    
    /* If test isn't available yet return nothing */   
    int64_t now = time(NULL);
    if (now < json_object_get_int64(start_time))
        return NULL;
    
    /* If results haven't been made available by examinator
     * remove correct answers */
    if (now < json_object_get_int64(end_time) ||
        json_object_get_boolean(results_available) != TRUE)
        json_object_object_del(test, "correctAnswers");

    json_object *answers = get_answers();
    json_object *user_record = get_user_answers_record(id, username, answers);
    
    /* If there is no answer record (i.e. student gets test for the first time)
     * create an empty one with current time logged */
    int64_t user_start_time = 0;
    if (!user_record) {
        if (now < json_object_get_int64(end_time)) {
            user_start_time = create_user_answers_record(id, username, answers);
            put_answers(answers);
        }
    /* Else if user has submitted answers add them to test */    
    } else {
        json_object *user_answers;
        if (json_object_object_get_ex(user_record, "answers", &user_answers) == TRUE &&
            !json_object_is_type(user_answers, json_type_null)) {
                json_object_object_add(test, "userAnswers", json_object_get(user_answers));
                json_object_object_add(test, "userSubmittedAnswers", json_object_new_boolean(TRUE));
        }
                
        json_object *creation_time;
        if (json_object_object_get_ex(user_record, "creationTime", &creation_time) == TRUE &&
            json_object_is_type(creation_time, json_type_int))
                user_start_time = json_object_get_int64(creation_time);
    }
    
    if (user_start_time != 0)
        json_object_object_add(test, "userStartTime", json_object_new_int64(user_start_time));

    json_object_put(answers);
    return test;
}

json_object *get_entity(const char *name, json_object *entities)
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
    
    /* Add userStartTime for each test for which user answer record exists */
    json_object *answers = get_answers();
    for (int i = 0; i < json_object_array_length(tests); i++) {
        json_object *test = json_object_array_get_idx(tests, i);
        json_object *test_id;
        if (json_object_object_get_ex(test, "id", &test_id) && 
                json_object_is_type(test_id, json_type_string)) {
            uuid_t id;
            uuid_parse(json_object_get_string(test_id), id);
            json_object *user_record = get_user_answers_record(id, username, answers);
            
            if (user_record) {
                json_object *creation_time;
                if (json_object_object_get_ex(user_record, "creationTime", &creation_time) == TRUE &&
                        json_object_is_type(creation_time, json_type_int))
                    json_object_object_add(test, "userStartTime", json_object_get(creation_time));
                
                json_object *user_answers;
                if (json_object_object_get_ex(user_record, "answers", &user_answers) == TRUE &&
                    !json_object_is_type(user_answers, json_type_null)) {
                        json_object_object_add(test, "userSubmittedAnswers", json_object_new_boolean(TRUE));
                }
            }
        }
            
    }
    json_object_put(answers);
     
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

int test_is_valid(json_object *test)
{
    if (!json_object_is_type(test, json_type_object))
        return 0;
        
    enum {
        HAS_NAME            = (1 << 0),
        HAS_TYPE            = (1 << 1),
        HAS_GROUPS          = (1 << 2),
        HAS_TIME_LIMIT      = (1 << 3),
        HAS_START_TIME      = (1 << 4),
        HAS_END_TIME        = (1 << 5),
        HAS_QUESTIONS       = (1 << 6),
        HAS_CORRECT_ANSWERS = (1 << 7)
    };

    struct json_object_iterator it = json_object_iter_begin(test);
    struct json_object_iterator it_end = json_object_iter_end(test);

    int key_flags = 0;
    
    while (!json_object_iter_equal(&it, &it_end)) {
        const char *key = json_object_iter_peek_name(&it);
        json_object *value = json_object_iter_peek_value(&it);

        if (streq(key, "name")) {
            if (!json_object_is_type(value, json_type_string))
                return 0;
            key_flags |= HAS_NAME;
        } else if (streq(key, "type")) {
            if (!json_object_is_type(value, json_type_string) ||
                (!streq(json_object_get_string(value), "single") &&
                !streq(json_object_get_string(value), "multi")))
                return 0;
            key_flags |= HAS_TYPE;
        } else if (streq(key, "groups")) {
            if (!json_object_is_type(value, json_type_array))
                return 0;
            key_flags |= HAS_GROUPS;
        } else if (streq(key, "timeLimit")) {
            if (!json_object_is_type(value, json_type_int) ||
                json_object_get_int64(value) <= 0)
                return 0;
            key_flags |= HAS_TIME_LIMIT;
        } else if (streq(key, "startTime")) {
            if (!json_object_is_type(value, json_type_int) ||
                json_object_get_int64(value) <= 0)
                return 0;
            key_flags |= HAS_START_TIME;
        } else if (streq(key, "endTime")) {
            if (!json_object_is_type(value, json_type_int) ||
                json_object_get_int64(value) <= 0)
                return 0;
            key_flags |= HAS_END_TIME;
        } else if (streq(key, "questions")) {
            if (!json_object_is_type(value, json_type_array))
                return 0;
            key_flags |= HAS_QUESTIONS;
        } else if (streq(key, "correctAnswers")) {
            if (!json_object_is_type(value, json_type_array))
                return 0;
            key_flags |= HAS_CORRECT_ANSWERS;
        } else
            return 0;
            
        json_object_iter_next(&it);
    }

    if (key_flags != (HAS_NAME | HAS_TYPE | HAS_GROUPS | HAS_TIME_LIMIT |
        HAS_START_TIME | HAS_END_TIME | HAS_QUESTIONS | HAS_CORRECT_ANSWERS))
        return 0;

    json_object *start_time;
    json_object *end_time;
    json_object_object_get_ex(test, "startTime", &start_time);
    json_object_object_get_ex(test, "endTime", &end_time);

    if (json_object_get_int64(start_time) > json_object_get_int64(end_time))
        return 0;
        
    return 1;
}

int submit_test(const char *username, json_object *test)
{
    if (!test_is_valid(test))
        return -1;

    json_object *tests = get_tests();

    uuid_t id;
    char id_string[37];
    uuid_generate(id);
    uuid_unparse(id, id_string);
    
    json_object_object_add(test, "id", json_object_new_string(id_string));
    json_object_object_add(test, "owner", json_object_new_string(username));
    json_object_object_add(test, "resultsAvailable", json_object_new_boolean(FALSE));
    
    json_object_array_add(tests, test);
    put_tests(tests);
    json_object_put(tests);
    
    return 0;
}

bool is_array_of_json_type(json_object *obj, json_type type)
{
    if (!json_object_is_type(obj, json_type_array))
        return false;
    for (int i = 0; i < json_object_array_length(obj); i++) {
        json_object *el = json_object_array_get_idx(obj, i);
        if (!json_object_is_type(el, type))
            return false;
    }
    return true;
}

struct key_type {
    const char *key;
    json_type type;
};

bool object_has_key_types(json_object *obj, const struct key_type *k)
{
    if (!json_object_is_type(obj, json_type_object))
        return false;

    int i = 0;
    for (; k->key; k++) {
        json_object *sub_obj;
        if (json_object_object_get_ex(obj, k->key, &sub_obj) != TRUE ||
            !json_object_is_type(sub_obj, k->type))
                return false;
        i++;
    }
    
    if (json_object_object_length(obj) != i)
        return false;
        
    return true;
}

bool group_is_valid(json_object *group)
{
    static const struct key_type keytypes[] = {
        { "name", json_type_string },
        { "fullName", json_type_string },
        { "members", json_type_array },
        { NULL, json_type_null }
    };

    if (!object_has_key_types(group, keytypes))
        return false;

    json_object *members;
    json_object_object_get_ex(group, "members", &members);
    if (!is_array_of_json_type(members, json_type_string))
        return false;

    return true;
}

bool groups_are_valid(json_object *groups)
{
    if (!json_object_is_type(groups, json_type_array))
        return false;
        
    for (int i = 0; i < json_object_array_length(groups); i++) {
        json_object *group = json_object_array_get_idx(groups, i);
        if (!group_is_valid(group))
            return false;
    }

    return true;
}

int submit_groups(json_object *groups)
{
    if (!groups_are_valid(groups))
        return -1;
        
    put_groups(groups);
    return 0;
}
