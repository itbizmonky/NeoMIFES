#include "neomifes/app/paint_deferral.h"

#include <gtest/gtest.h>

using neomifes::app::shouldPaintNow;

TEST(PaintDeferralTest, PaintsWhenQueueEmpty) {
    EXPECT_TRUE(shouldPaintNow(/*moreKeyboardInputQueued=*/false, /*paintOverdue=*/false));
}

TEST(PaintDeferralTest, DefersWhenQueueHasMoreAndNotOverdue) {
    EXPECT_FALSE(shouldPaintNow(/*moreKeyboardInputQueued=*/true, /*paintOverdue=*/false));
}

TEST(PaintDeferralTest, SafetyValveForcesPaintEvenWithQueueBacklog) {
    EXPECT_TRUE(shouldPaintNow(/*moreKeyboardInputQueued=*/true, /*paintOverdue=*/true));
}

TEST(PaintDeferralTest, PaintsWhenQueueEmptyAndOverdue) {
    EXPECT_TRUE(shouldPaintNow(/*moreKeyboardInputQueued=*/false, /*paintOverdue=*/true));
}
