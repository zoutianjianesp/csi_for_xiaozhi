#include "echoear_tools.h"
#include "EchoEar.h"
#include "echo_base_control.h"
#include "audio_analysis.h"
#include "mcp_server.h"
#include "board.h"
#include "assets/lang_config.h"
#include <esp_log.h>
#include "customer_ui/alarm_api.h"
#include "customer_ui/alarm_manager.h"
#include "ui_bridge.h"
#define TAG "EchoEarTools"

void EchoEarTools::Initialize(EspS3Cat* board)
{
    auto &mcp_server = McpServer::GetInstance();
    char buffer[1024]; // 确保缓冲区足够大
    
    // 使用sprintf将多个字符串拼接成一个完整的JSON格式字符串
    sprintf(buffer, 
        "\"Echo base action control. Available actions:\\n\" "
        "\"shark_head: %s\\n\" "
        "\"shark_head_decay: %s\\n\" "
        "\"look_around: %s\\n\" "
        "\"beat_swing: %s\\n\" "
        "\"cat_nuzzle: %s\\n\" "
        "\"calibrate: %s\\n\""
        "\"go_home: %s\\n\"",
        Lang::Strings::SHAKE_HEAD_ACTION,
        Lang::Strings::SHAKE_HEAD_DECAY_ACTION,
        Lang::Strings::LOOK_AROUND_ACTION,
        Lang::Strings::BEAT_SWING_ACTION,
        Lang::Strings::CAT_NUZZLE_ACTION,
        Lang::Strings::CALIBRATE_ACTION,
        Lang::Strings::SWITCH_HOME);
    // Echo base action control
    mcp_server.AddTool("self.echo_base.set_action", buffer,
    PropertyList({
        Property("action", kPropertyTypeString),
    }), [board](const PropertyList & properties) -> ReturnValue {
        const std::string &action = properties["action"].value<std::string>();
        int action_value = -1;

        ESP_LOGI(TAG, "&&& Do Action: %s", action.c_str());
        if (action == "shark_head") {
            action_value = ECHO_BASE_CMD_SET_ACTION_SHARK_HEAD;
        } else if (action == "shark_head_decay") {
            action_value = ECHO_BASE_CMD_SET_ACTION_SHARK_HEAD_DECAY;
        } else if (action == "look_around")
        {
            action_value = ECHO_BASE_CMD_SET_ACTION_LOOK_AROUND;
        } else if (action == "beat_swing")
        {
            action_value = ECHO_BASE_CMD_SET_ACTION_BEAT_SWING;
        } else if (action == "cat_nuzzle")
        {
            action_value = ECHO_BASE_CMD_SET_ACTION_CAT_NUZZLE;
        } else if (action == "calibrate")
        {
            echo_base_control_set_calibrate();
            BaseControl* base_control = board->GetBaseControl();
            if (base_control != nullptr) {
                bool completed = base_control->WaitForCalibrationComplete(30000);
                if (!completed) {
                    ESP_LOGW(TAG, "Calibration wait timeout");
                    return false;
                }
            }
        } else if (action == "go_home")
        {
            main_ui_switch_page(UI_BRIDGE_PAGE_HOME);
        } else
        {
            return false;
        }

        if (action_value != -1)
        {
            esp_err_t ret = echo_base_control_set_action(action_value);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to set action: %d", ret);
                return false;
            }
        }
        return true;
    });
    // 使用sprintf将多个字符串拼接成一个完整的JSON格式字符串
    sprintf(buffer,
        "\"Set audio analysis mode. Available modes:\\n\" "
        "\"beat_detection: %s\\n\" "
        "\"doa_follow: %s\\n\" "
        "\"disabled: %s\\n\"",
        Lang::Strings::BEAT_DETECT_SET,
        Lang::Strings::DOA_FOLLOW_SET,
        Lang::Strings::DISALED_SET);

    // Audio analysis mode control
    mcp_server.AddTool("self.echo_base.set_audio_mode", buffer,
    PropertyList({
        Property("mode", kPropertyTypeString),
    }), [board](const PropertyList & properties) -> ReturnValue {
        const std::string &mode = properties["mode"].value<std::string>();
        AudioAnalysisMode analysis_mode = AudioAnalysisMode::DISABLED;

        if (mode == "beat_detection") {
            analysis_mode = AudioAnalysisMode::BEAT_DETECTION;
        } else if (mode == "doa_follow") {
            analysis_mode = AudioAnalysisMode::DOA_FOLLOW;
        } else if (mode == "disabled")
        {
            analysis_mode = AudioAnalysisMode::DISABLED;
        } else
        {
            ESP_LOGE(TAG, "Unknown audio analysis mode: %s", mode.c_str());
            return false;
        }

        board->SetAudioAnalysisMode(analysis_mode);
        ESP_LOGI(TAG, "Audio analysis mode set to: %s", mode.c_str());
        return true;
    });

    // Pomodoro timer control
    mcp_server.AddTool("self.pomodoro.start", Lang::Strings::START_POMODORO,
    PropertyList({
        Property("minutes", kPropertyTypeInteger, 5, 1, 60),
    }), [](const PropertyList& properties) -> ReturnValue {
        int minutes = properties["minutes"].value<int>();
        ESP_LOGI(TAG, "Starting pomodoro timer with %d minutes", minutes);
        alarm_start_pomodoro(minutes);
        return true;
    });

    // Pomodoro timer control (start/pause)
    mcp_server.AddTool("self.pomodoro.control", Lang::Strings::CONTROL_POMODORO,
    PropertyList({
        Property("action", kPropertyTypeString),
    }), [](const PropertyList& properties) -> ReturnValue {
        const std::string &action = properties["action"].value<std::string>();
        
        bool success = false;
        if (action == "start") {
            ESP_LOGI(TAG, "Starting pomodoro timer");
            success = alarm_resume_pomodoro();
        } else if (action == "pause") {
            ESP_LOGI(TAG, "Pausing pomodoro timer");
            success = alarm_pause_pomodoro();
        } else {
            ESP_LOGE(TAG, "Unknown pomodoro action: %s (expected 'start' or 'pause')", action.c_str());
            return false;
        }
        return success;
    });

    // Sleep timer control
    mcp_server.AddTool("self.sleep.start", Lang::Strings::START_SLEEP,
    PropertyList({
        Property("end_hour", kPropertyTypeInteger, 8, 0, 23),
        Property("end_min", kPropertyTypeInteger, 0, 0, 59),
    }), [](const PropertyList& properties) -> ReturnValue {
        int end_hour = properties["end_hour"].value<int>();
        int end_min = properties["end_min"].value<int>();
        
        // Validate parameters
        if (end_hour < 0 || end_hour >= 24) {
            ESP_LOGE(TAG, "Invalid end_hour: %d (must be 0-23)", end_hour);
            return false;
        }
        if (end_min < 0 || end_min >= 60) {
            ESP_LOGE(TAG, "Invalid end_min: %d (must be 0-59)", end_min);
            return false;
        }
        
        ESP_LOGI(TAG, "Setting sleep timer: current time -> %02d:%02d", end_hour, end_min);
        alarm_start_sleep(end_hour, end_min);
        return true;
    });
}
