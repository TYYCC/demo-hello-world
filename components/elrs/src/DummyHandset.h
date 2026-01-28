#pragma once
#include "handset.h"
#include "common.h" // ChannelData + expresslrs_mod_settings_s

class DummyHandset : public Handset {
public:
    DummyHandset();

    void JustSentRFpacket() override;

    void registerCallbacks(void (*connectCb)(), void (*disconnectCb)());

    void setChannelValue(uint32_t ch, uint16_t value);
    uint16_t getChannelValue(uint32_t ch) const;

    void Begin() override { /* do nothing */ }
    void End() override { /* do nothing */ }
    void handleInput() override { /* do nothing */ }

    static const int NUM_CHANNELS = 16;

private:
    void clampChannel(uint32_t &ch);

    uint16_t internalCh[NUM_CHANNELS];
};

// 全局 Dummy 手柄指针
extern DummyHandset* dummyHandset;

// 初始化虚拟手柄
void initVirtualHandset();
