#ifndef APPLICATION_H
#define APPLICATION_H

#include <memory>
#include <string>

struct ApplicationState;
struct UiActions;

class Application {
public:
    Application();
    ~Application();

    int Run(bool daemonize);

private:
    bool DetachFromTerminal();
    void InitializeState();
    void JoinBackgroundTasks();
    void StartTranscriptLoad(const std::string& audioPath,
                             const std::string& transcriptDirectory,
                             const std::string& transcriptPath = {},
                             const std::string& databasePath = {},
                             const std::string& databaseKey = {});
    void StartDatabaseSync();
    void StartGlobalSearch(const std::string& query);
    void SelectAudioFile(const std::string& filename);
    void SelectSearchResult(const struct GlobalSearchResult& result);
    UiActions BuildActions();

    std::unique_ptr<ApplicationState> state_;
};

#endif
