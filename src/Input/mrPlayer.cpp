#include "pch.h"
#include "mrInternal.h"

namespace mr {

class Player : public RefCount<IPlayer>
{
public:
    Player();
    ~Player();
    bool start(uint32_t loop) override;
    bool stop() override;
    bool isPlaying() const override;
    bool update() override;
    bool load(const char* path) override;
    void setMatchTarget(MatchTarget v) override;

    bool execRecord(const OpRecord& rec);

private:
    bool m_playing = false;
    millisec m_time_start_real = 0;
    millisec m_time_start = 0;
    millisec m_time_wait = 0;
    uint32_t m_record_index = 0;
    uint32_t m_loop_required = 0, m_loop_count = 0;
    std::vector<OpRecord> m_records;

    struct State
    {
        int2 mouse_pos{};
    };
    State m_state;
    std::map<int, State> m_mouse_state_slots;

    MatchTarget m_match_target = MatchTarget::EntireScreen;
    IScreenMatcherPtr m_smatch;
};


Player::Player()
{
}

Player::~Player()
{
}

bool Player::start(uint32_t loop)
{
    if (m_playing || m_records.empty())
        return false;

    m_time_start_real = m_time_start = NowMS();
    m_loop_required = loop;
    m_loop_count = 0;
    m_record_index = 0;
    m_playing = true;

    CURSORINFO ci;
    ci.cbSize = sizeof(ci);
    ::GetCursorInfo(&ci);
    m_state.mouse_pos = (int2&)ci.ptScreenPos;
    return true;
}

bool Player::stop()
{
    if (!m_playing)
        return false;

    m_playing = false;
    return true;
}

bool Player::isPlaying() const
{
    return this && m_playing;
}

bool Player::update()
{
    if (!m_playing || m_records.empty())
        return false;


    // execute records
    for (;;) {
        const auto& rec = m_records[m_record_index];

        millisec time_before_exec = NowMS();
        millisec time_rec = time_before_exec - m_time_start;

        if (time_rec >= rec.time) {
            auto go_next = execRecord(rec);

            millisec time_after_exec = NowMS();
            millisec timestamp = time_after_exec - m_time_start_real;
            millisec elapsed = time_after_exec - time_before_exec;
            if (!go_next) {
                m_time_start = time_after_exec - rec.time;
                break;
            }
            else if (!m_playing) {
                break;
            }

            mrDbgPrint("record executed (%ld %ld ms): %s\n", timestamp, elapsed, rec.toText().c_str());
            ++m_record_index;

            if (rec.type == OpType::MouseMoveMatch) {
                // adjust time if MouseMoveMatch because it is very slow and causes input hiccup
                m_time_start += elapsed;
            }
            else if (rec.type == OpType::TimeShift) {
                // handle time shift
                m_time_start = time_after_exec - rec.time + rec.exdata.time_shift;
            }
            else if (rec.type == OpType::Repeat) {
                // rewind time and record index
                m_time_start = NowMS() - rec.exdata.repeat_point;

                auto it = std::lower_bound(m_records.begin(), m_records.end(), rec.exdata.repeat_point,
                    [](const OpRecord& r, int t) { return r.time < t; });
                m_record_index = (uint32_t)std::distance(m_records.begin(), it);
                break;
            }

            if (m_record_index >= m_records.size()) {
                // go next loop or stop
                m_record_index = 0;
                m_time_start = NowMS();
                ++m_loop_count;
                if (m_loop_count >= m_loop_required) {
                    m_playing = false;
                    return false;
                }
                else {
                    break;
                }
            }
        }
        else {
            break;
        }
    }
    return true;
}

bool Player::execRecord(const OpRecord& rec)
{
    auto send = [](INPUT& v) {
        ::SendInput(1, &v, sizeof(INPUT));
    };

    auto make_mouse_move = [](INPUT& input, int2 screen_pos) {
        // http://msdn.microsoft.com/en-us/library/ms646260(VS.85).aspx
        // If MOUSEEVENTF_ABSOLUTE value is specified, dx and dy contain normalized absolute coordinates between 0 and 65,535.
        // The event procedure maps these coordinates onto the display surface.
        // Coordinate (0,0) maps onto the upper-left corner of the display surface, (65535,65535) maps onto the lower-right corner.

        static float2 s2c = 65535.0f / float2{
            float(::GetSystemMetrics(SM_CXSCREEN)),
            float(::GetSystemMetrics(SM_CYSCREEN))
        };
        int2 cpos = int2(float2(screen_pos) * s2c);
        input.mi.dx = cpos.x;
        input.mi.dy = cpos.y;
        input.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;
    };

    auto do_match = [this, &rec]() {
        std::vector<ITemplatePtr> templates;
        for (auto& i : rec.exdata.templates)
            if (i.tmpl)
                templates.push_back(i.tmpl);

        auto match_target = ::GetForegroundWindow();
        auto r = m_smatch->match(templates, match_target);
        mrDbgPrint("match score: %.2f (%d, %d)\n", r.score, r.region.getCenter().x, r.region.getCenter().y);
        return r;
    };

    bool ret = true;
    switch (rec.type)
    {
    case OpType::MouseDown:
    {
        INPUT input{ INPUT_MOUSE };
        switch (rec.data.mouse.button) {
        case 1: input.mi.dwFlags |= MOUSEEVENTF_LEFTDOWN; break;
        case 2: input.mi.dwFlags |= MOUSEEVENTF_RIGHTDOWN; break;
        case 3: input.mi.dwFlags |= MOUSEEVENTF_MIDDLEDOWN; break;
        default: break;
        }
        send(input);
        break;
    }

    case OpType::MouseUp:
    {
        INPUT input{ INPUT_MOUSE };
        switch (rec.data.mouse.button) {
        case 1: input.mi.dwFlags |= MOUSEEVENTF_LEFTUP; break;
        case 2: input.mi.dwFlags |= MOUSEEVENTF_RIGHTUP; break;
        case 3: input.mi.dwFlags |= MOUSEEVENTF_MIDDLEUP; break;
        default: break;
        }
        send(input);
        break;
    }

    case OpType::MouseMoveAbs:
    {
        INPUT input{ INPUT_MOUSE };
        m_state.mouse_pos = rec.data.mouse.pos;
        make_mouse_move(input, m_state.mouse_pos);
        send(input);
        break;
    }

    case OpType::MouseMoveRel:
    {
        INPUT input{ INPUT_MOUSE };
        m_state.mouse_pos += rec.data.mouse.pos;
        make_mouse_move(input, m_state.mouse_pos);
        send(input);
        break;
    }

    case OpType::MouseMoveMatch:
    {
        INPUT input{ INPUT_MOUSE };
        auto r = do_match();
        if (r.score <= rec.exdata.match_threshold) {
            m_state.mouse_pos = r.region.getCenter();
            make_mouse_move(input, m_state.mouse_pos);
            send(input);
        }
        else {
            stop();
        }
        break;
    }

    case OpType::SaveMousePos:
    {
        m_mouse_state_slots[rec.exdata.save_slot] = m_state;
        break;
    }
    case OpType::LoadMousePos:
    {
        auto i = m_mouse_state_slots.find(rec.exdata.save_slot);
        if (i != m_mouse_state_slots.end()) {
            m_state = i->second;

            INPUT input{};
            input.type = INPUT_MOUSE;
            make_mouse_move(input, m_state.mouse_pos);

            // it seems single mouse move can't step over display boundary. so SendInput twice.
            send(input);
            send(input);
        }
        break;
    }

    case OpType::KeyDown:
    case OpType::KeyUp:
    {
        INPUT input{ INPUT_KEYBOARD };
        input.ki.wVk = (WORD)rec.data.key.code;
        if (rec.type == OpType::KeyUp)
            input.ki.dwFlags |= KEYEVENTF_KEYUP;
        send(input);
        break;
    }

    case OpType::Wait:
    {
        if (m_time_wait == 0)
            m_time_wait = NowMS();

        if ((NowMS() - m_time_wait) >= rec.exdata.wait_time) {
            m_time_wait = 0;
        }
        else {
            ret = false;
        }
        break;
    }

    case OpType::WaitUntilMatch:
    {
        auto r = do_match();
        if (r.score <= rec.exdata.match_threshold) {
            // ok
        }
        else {
            WaitVSync();
            ret = false;
        }
        break;
    }

    default:
        break;
    }
    return ret;
}

bool Player::load(const char* path)
{
    m_records.clear();

    std::ifstream ifs(path, std::ios::in);
    if (!ifs)
        return false;

    std::string l;
    while (std::getline(ifs, l)) {
        OpRecord rec;
        if (rec.fromText(l)) {
            if (rec.type == OpType::MatchParams) {
                m_smatch = CreateScreenMatcher(rec.exdata.match_params);
            }

            if (!rec.exdata.templates.empty()) {
                if (!m_smatch)
                    m_smatch = CreateScreenMatcher();

                for (auto& id : rec.exdata.templates) {
                    id.tmpl = m_smatch->createTemplate(id.path.c_str());
                    if (id.tmpl) {
                        id.tmpl->setMatchPattern(rec.exdata.match_pattern);
                    }
                    else {
                        mrDbgPrint("*** failed to load template %s ***\n", id.path.c_str());
                    }
                }
            }

            m_records.push_back(rec);
        }
    }
    std::stable_sort(m_records.begin(), m_records.end(),
        [](auto& a, auto& b) { return a.time < b.time; });

    return !m_records.empty();
}

void Player::setMatchTarget(MatchTarget v)
{
    m_match_target = v;
}

mrAPI IPlayer* CreatePlayer_()
{
    return new Player();
}

} // namespace mr
