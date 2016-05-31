#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <json-c/json.h>
#include <uuid/uuid.h>

#include "common.h"
#include "db.h"

#define TESTS_FILENAME      "tests"
#define ANSWERS_FILENAME    "answers"
#define USERS_FILENAME      "users"
#define GROUPS_FILENAME     "groups"

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

/**
 * Wczytuje plik do dynamicznie alokowanego bufora znakowego.
 * Wstawia null na końcu bufora.
 * Wywołujący jest odpowiedzialny za zwolnienie pamięci.
 *
 * @param fp plik
 *
 * @returns Wskaźnik do utworzonego napisu lub NULL w przypdaku niepowodzenia
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
    char *buf = malloc(sizeof(char) * (bufsize + 1));
    if (!buf) {
        log_errno("malloc");
        return NULL;
    }

    /* Go back to the start of the file. Read the entire file into memory. */
    if (fseek(fp, 0L, SEEK_SET) != 0 || fread(buf, sizeof(char), bufsize, fp) != bufsize) {
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
    return get_json_from_file(answers_file);
}

json_object *get_test(uuid_t id, json_object *tests)
{
    if (!json_object_is_type(tests, json_type_array))
        return NULL;
        
    for (int i = 0; i < json_object_array_length(tests); i++) {
        json_object *test = json_object_array_get_idx(tests, i);
        json_object *subobj;
        if (json_object_object_get_ex(test, "id", &subobj) == TRUE) {
            uuid_t id_in_json;
            if (uuid_parse(json_object_get_string(subobj), id_in_json) == 0)
                if (uuid_compare(id, id_in_json) == 0)
                    return test;
        }
    }

    return NULL;
}

/* entity: user or group */
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

json_object *get_test_headers(json_object *tests)
{
    if (!json_object_is_type(tests, json_type_array))
        return NULL;
        
    for (int i = 0; i < json_object_array_length(tests); i++) {
        json_object *test = json_object_array_get_idx(tests, i);
        if (!json_object_is_type(test, json_type_object))
            return NULL;
        json_object_object_del(test, "questions");
        json_object_object_del(test, "correctAnswers");
    }

    return tests;
}

/* allocates memory! */
json_object *get_tests_for_examiner(const char *username)
{
    json_object *tests = get_tests();
    if (!json_object_is_type(tests, json_type_array))
        return NULL;

    json_object *examiner_tests = json_object_new_array();

    for (int i = 0; i < json_object_array_length(tests); i++) {
        json_object *test = json_object_array_get_idx(tests, i);
        json_object *owner;
        if (json_object_object_get_ex(test, "owner", &owner) == TRUE) {
            if (!json_object_is_type(owner, json_type_string))
                return NULL;
            if (strcmp(json_object_get_string(owner), username) == 0)
                json_object_array_add(examiner_tests, json_object_get(test));
        }
        
    }

    json_object_put(tests);
    
    return examiner_tests;
}

/* allocates memory! */
json_object *get_tests_for_student(const char *username)
{
    json_object *tests = get_tests();
    if (!json_object_is_type(tests, json_type_array))
        return NULL;

    json_object *groups = get_groups();
    json_object *student_tests = json_object_new_array();

    for (int i = 0; i < json_object_array_length(tests); i++) {
        json_object *test = json_object_array_get_idx(tests, i);
        json_object *test_groups;
        if (json_object_object_get_ex(test, "groups", &test_groups) == TRUE) {
            if (!json_object_is_type(test_groups, json_type_array))
                return NULL;
            for (int i = 0; i < json_object_array_length(test_groups); i++) {
                json_object *test_group = json_object_array_get_idx(test_groups, i);
                if (!json_object_is_type(test_group, json_type_string))
                    return NULL;
                if (user_is_group_member(username, json_object_get_string(test_group), groups)) {
                    json_object_array_add(student_tests, json_object_get(test));
                    break;
                }
            }
        }
    }

    json_object_put(groups);
    json_object_put(tests);
    
    return student_tests;
}
