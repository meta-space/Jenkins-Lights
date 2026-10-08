#include <unity.h>
#include "../../src/jenkins.h"

void setUp() {}
void tearDown() {}

void test_parse_and_overall() {
    JsonDocument doc;
    deserializeJson(doc, R"({"jobs":[
        {"fullName":"app","color":"blue","lastCompletedBuild":{"number":7,"result":"SUCCESS"}},
        {"fullName":"lib","color":"yellow_anime","lastCompletedBuild":{"number":3,"result":"UNSTABLE"}},
        {"fullName":"f","jobs":[{"fullName":"f/mb","jobs":[
            {"fullName":"f/mb/feature%2Ffoo","color":"red","lastCompletedBuild":{"number":1,"result":"FAILURE"}}]}]},
        {"fullName":"empty-folder"},
        {"fullName":"new","color":"notbuilt","lastCompletedBuild":null}]})");
    std::vector<Job> jobs;
    collectJobs(doc["jobs"].as<JsonArrayConst>(), jobs);

    TEST_ASSERT_EQUAL(4, jobs.size());
    TEST_ASSERT_EQUAL_STRING("f/mb/feature%2Ffoo", jobs[2].name.c_str());
    TEST_ASSERT_EQUAL(7, jobs[0].number);
    TEST_ASSERT_TRUE(jobs[1].building);
    TEST_ASSERT_EQUAL_STRING("", jobs[3].result.c_str());

    TEST_ASSERT_EQUAL_STRING("green", overall(jobs, {"app"}));
    TEST_ASSERT_EQUAL_STRING("red", overall(jobs, {"app", "f/mb/feature%2Ffoo"}));
    TEST_ASSERT_EQUAL_STRING("orange", overall(jobs, {"f/mb/feature%2Ffoo", "lib"}));  // building wins
    TEST_ASSERT_EQUAL_STRING("off", overall(jobs, {"new"}));
    TEST_ASSERT_EQUAL_STRING("off", overall(jobs, {}));

    jobs[1].building = false;  // UNSTABLE counts as failed, like the desktop tool
    TEST_ASSERT_EQUAL_STRING("red", overall(jobs, {"app", "lib"}));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_parse_and_overall);
    return UNITY_END();
}
