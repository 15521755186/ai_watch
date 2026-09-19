#ifndef OLED_DISPLAY_H
#define OLED_DISPLAY_H

#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>

#include "display.h"

class OledDisplay : public Display {
private:
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;

    lv_obj_t* default_screen = nullptr;
    lv_obj_t* status_bar_ = nullptr;
    lv_obj_t* content_ = nullptr;
    lv_obj_t* content_left_ = nullptr;
    lv_obj_t* content_right_ = nullptr;
    lv_obj_t* switch_header_ = nullptr;
    lv_obj_t* switch_title_label_ = nullptr;
    lv_obj_t* switch_hint_label_ = nullptr;
    lv_obj_t* switch_indicator_label_ = nullptr;
    DisplayPage home_options[Switch_Count] = {
        {&zhuye, "\xE4\xB8\xBB\xE9\xA1\xB5"},
        {&duihua, "\x41\x49\xE5\xAF\xB9\xE8\xAF\x9D"},
        {&jiance, "\xE5\x81\xA5\xE5\xBA\xB7"},
        {&tianqi, "\xE5\xA4\xA9\xE6\xB0\x94"},
        {&shoudiantong, "\xE6\x89\x8B\xE7\x94\xB5"},
        {&miaobiao, "\xE7\xA7\x92\xE8\xA1\xA8"},
        {&naozhong, "\xE9\x97\xB9\xE9\x92\x9F"},
        {&mpu, "\xE5\xA7\xBF\xE6\x80\x81"},
        {&yundong, "\xE8\xBF\x90\xE5\x8A\xA8"},
        {&yinyue, "\xE9\x9F\xB3\xE4\xB9\x90"},
        {&shezhi, "\xE8\xAE\xBE\xE7\xBD\xAE"},
        {&youxi, "\xE5\xA8\xB1\xE4\xB9\x90"},
        {&naozhong, "\xE6\x97\xB6\xE9\x97\xB4"},
        {&shezhi, "\xE9\x87\x8D\xE6\x96\xB0\xE9\x85\x8D\xE7\xBD\x91"},
        {&guanyu, "\xE5\x85\xB3\xE4\xBA\x8E"},
    };
    lv_anim_t anim;
    static const int ANIM_DURATION = 500;
    DisplayFonts fonts_;

    bool Lock(int timeout_ms = 0) override;
    void Unlock() override;

    void Switch_Page_Init(lv_obj_t* screen);
    void Boot_Page_Init(lv_obj_t* screen);
    void Main_Page_Init(lv_obj_t* screen);
    void Health_Check_Page_Init(lv_obj_t* screen);
    void Dialougue_Page_Init(lv_obj_t* screen);
    void Wait_Page_Init(lv_obj_t* screen);
    void Weather_Page_Init(lv_obj_t* screen);
    void About_Page_Init(lv_obj_t* screen);
    void Reconfig_Page_Init(lv_obj_t* screen);
    void Flashlight_Page_Init(lv_obj_t* screen);
    void Miaobiao_Page_Init(lv_obj_t* screen);
    void Naozhong_Page_Init(lv_obj_t* screen);
    void Mpu6050_Page_Init(lv_obj_t* screen);
    void Sport_Page_Init(lv_obj_t* screen);
    void Fun_Page_Init(lv_obj_t* screen);
    void Game_Page_Init(lv_obj_t* screen);
    void Reflex_Page_Init(lv_obj_t* screen);
    void Memory_Page_Init(lv_obj_t* screen);
    void Breathe_Page_Init(lv_obj_t* screen);
    void Quick_Page_Init(lv_obj_t* screen);
    void Sound_Page_Init(lv_obj_t* screen);
    void Time_Setting_Page_Init(lv_obj_t* screen);

    void next(void) override;
    void prev(void) override;
    void OnSwitchPageShown() override;

    void SetupUI_128x64();
    void SetupUI_128x32();
    void slide_to(OledDisplaySwitch new_index);
    void UpdateSwitchPagePosition();

public:
    OledDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width, int height,
        bool mirror_x, bool mirror_y, DisplayFonts fonts);
    ~OledDisplay();

    void SetChatMessage(const char* role, const char* content) override;
    void SetFlashlightText(const char* text) override;
};

#endif
