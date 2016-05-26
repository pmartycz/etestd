#include <stdio.h>
#include <json-c/json.h>
#include <uuid/uuid.h>

#include "db.h"

struct json_object *get_test(uuid_t id, struct json_object *tests)
{
    if (!json_object_is_type(tests, json_type_array))
        return NULL;
        
    for (int i = 0; i < json_object_array_length(tests); i++) {
        struct json_object *test = json_object_array_get_idx(tests, i);
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

/**
 * Wyślij do strumienia nagłówki testów. tests powinien zawierać
 * listę testów wczytaną z pliku z testami.
 *
 * @param tests lista testów wczytana z pliku z testami
 * @param fp strumień do zapisu
 *
 * @returns 0 w przypadku sukcesu. W przeciwnym wypadku -1.
 */  
int write_test_headers(struct json_object *tests, FILE *fp)
{
    if (!json_object_is_type(tests, json_type_array))
        return -1;
        
    for (int i = 0; i < json_object_array_length(tests); i++) {
        struct json_object *test = json_object_array_get_idx(tests, i);
        if (!json_object_is_type(test, json_type_object))
            return -1;
        json_object_object_del(test, "questions");
        json_object_object_del(test, "correctAnswers");
    }

    if (fputs(json_object_to_json_string_ext(tests, JSON_C_TO_STRING_PLAIN), fp) == EOF)
        return -1;
    return 0;
}
