#pragma once
#include "mrFoundation.h"
#include "mrGfx.h"

namespace mr {

enum class OpType : int
{
    Unknown,
    KeyDown,
    KeyUp,
    MouseDown,
    MouseUp,
    MouseMoveAbs,
    MouseMoveRel,
    SaveMousePos,
    LoadMousePos,
    MatchParams,
    MouseMoveMatch,
    Wait,
    WaitUntilMatch,
    TimeShift,
    Repeat,
};

struct OpRecord
{
    OpType type = OpType::Unknown;
    uint32_t time{}; // in millisec
    union
    {
        struct
        {
            int2 pos;
            int button;
        } mouse;
        struct
        {
            int code;
        } key;
    } data{};


    struct TemplateData
    {
        std::string path;
        ITemplatePtr tmpl{};
    };

    struct
    {
        int wait_time = 0;
        int time_shift = 0;
        int repeat_point = 0;
        int save_slot = 0;
        IScreenMatcher::Params match_params{};
        float match_threshold = 0.2f;
        ITemplate::MatchPattern match_pattern{};
        std::vector<TemplateData> templates;
    } exdata{};

    std::string toText() const;
    bool fromText(const std::string& v);
};
using OpRecordHandler = std::function<bool (OpRecord& rec)>;

enum class MatchTarget
{
    EntireScreen,
    ForegroundWindow,
};


mrDeclPtr(IRecorder);
mrDeclPtr(IPlayer);

class IRecorder : public IObject
{
public:
    virtual bool start() = 0;
    virtual bool stop() = 0;
    virtual bool isRecording() const = 0;
    virtual bool update() = 0;
    virtual bool save(const char* path) const = 0;

    virtual void addRecord(const OpRecord& rec) = 0;
};
mrAPI IRecorder* CreateRecorder_();
mrDefShared(CreateRecorder);


class IPlayer : public IObject
{
public:
    virtual bool start(uint32_t loop = 1) = 0;
    virtual bool stop() = 0;
    virtual bool isPlaying() const = 0;
    virtual bool update() = 0;
    virtual bool load(const char* path) = 0;
    virtual void setMatchTarget(MatchTarget v) = 0;
};
mrAPI IPlayer* CreatePlayer_();
mrDefShared(CreatePlayer);


class IInputReceiver
{
public:
    virtual bool valid() const = 0;
    virtual void update() = 0;
    virtual int addHandler(OpRecordHandler v) = 0;
    virtual void removeHandler(int i) = 0;
    virtual int addRecorder(OpRecordHandler v) = 0;
    virtual void removeRecorder(int i) = 0;

protected:
    virtual ~IInputReceiver() {}
};
mrAPI IInputReceiver* GetReceiver();

struct Key
{
    uint32_t ctrl : 1;
    uint32_t alt : 1;
    uint32_t shift : 1;
    uint32_t code : 29;
};
inline bool operator<(const Key& a, const Key& b) { return (uint32_t&)a < (uint32_t&)b; }
inline bool operator>(const Key& a, const Key& b) { return (uint32_t&)a > (uint32_t&)b; }
inline bool operator==(const Key& a, const Key& b) { return (uint32_t&)a == (uint32_t&)b; }

std::map<Key, std::string> LoadKeymap(const char* path, const std::function<void(Key key, std::string path)>& body);

} // namespace mr
