#include "neomifes/ui/toast_state.h"

#include <gtest/gtest.h>

namespace neomifes::ui {
namespace {

TEST(ToastStateTest, InitiallyNotVisibleWithEmptyMessage) {
    const ToastState toast;
    EXPECT_FALSE(toast.isVisible());
    EXPECT_TRUE(toast.message().empty());
}

TEST(ToastStateTest, ShowSetsMessageAndMarksVisible) {
    ToastState toast;
    toast.show(u"hello");
    EXPECT_TRUE(toast.isVisible());
    EXPECT_EQ(toast.message(), u"hello");
}

TEST(ToastStateTest, ShowOverwritesThePreviousPendingMessage) {
    ToastState toast;
    toast.show(u"first");
    toast.show(u"second");
    EXPECT_TRUE(toast.isVisible());
    EXPECT_EQ(toast.message(), u"second");
}

TEST(ToastStateTest, HideClearsMessageAndVisibility) {
    ToastState toast;
    toast.show(u"hello");
    toast.hide();
    EXPECT_FALSE(toast.isVisible());
    EXPECT_TRUE(toast.message().empty());
}

}  // namespace
}  // namespace neomifes::ui
