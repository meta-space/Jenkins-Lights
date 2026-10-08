#pragma once
// Jenkins job list parsing and the traffic light rule, ported from the JenkinsStatus desktop tool (Jenkins.cs).
// Plain C++ (no Arduino.h) so it runs in the host unit test: pio test -e native
#include <ArduinoJson.h>
#include <set>
#include <string>
#include <vector>

#define JOB_FIELDS "fullName,color,lastCompletedBuild%5Bnumber,result%5D"
// jobs[F,jobs[F,jobs[F]]]: 3 nesting levels (folder > multibranch > branch); add a level if deeper folders show up.
const char* const TREE = "jobs%5B" JOB_FIELDS ",jobs%5B" JOB_FIELDS ",jobs%5B" JOB_FIELDS "%5D%5D%5D";

struct Job {
    std::string name;    // Jenkins fullName, e.g. "folder/repo/feature%2Ffoo" (branch names stay escaped)
    bool building;
    std::string result;  // last completed build: SUCCESS, FAILURE, UNSTABLE, ABORTED, ... or ""
    long number;
};

// Folders and multibranch projects have "jobs"; at the deepest level they have no "color". Only real jobs are kept.
inline void collectJobs(JsonArrayConst jobs, std::vector<Job>& out) {
    for (JsonObjectConst j : jobs) {
        std::string color = j["color"] | "";
        if (j["jobs"].is<JsonArrayConst>()) {
            collectJobs(j["jobs"].as<JsonArrayConst>(), out);
        } else if (!color.empty()) {
            out.push_back({
                j["fullName"] | "", 
                color.find("_anime") != std::string::npos,
                j["lastCompletedBuild"]["result"] | "", 
                j["lastCompletedBuild"]["number"] | 0L
            });
        }
    }
}

// Desktop priority: building > failed (FAILURE or UNSTABLE) > success > off (the desktop's gray).
inline const char* overall(const std::vector<Job>& jobs, const std::set<std::string>& watched) {
    bool building = false, failed = false, success = false;
    for (const Job& j : jobs) {
        if (!watched.count(j.name)) continue;
        building |= j.building;
        failed |= j.result == "FAILURE" || j.result == "UNSTABLE";
        success |= j.result == "SUCCESS";
    }
    return building ? "orange" : failed ? "red" : success ? "green" : "off";
}
