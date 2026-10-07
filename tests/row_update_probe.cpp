// row_update_probe.cpp — micro-measure: cost of calling update() on N plain
// child widgets inside a scroll area (what the Layers panel sweep does with
// 4101 rows). Throwaway diagnostic, not registered as a test.
#include <QApplication>
#include <QScrollArea>
#include <QScrollBar>
#include <QVBoxLayout>
#include <QVector>
#include <QWidget>

#include <chrono>
#include <cstdio>
#include <cstdlib>

using cnow = std::chrono::steady_clock;

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    const int n = argc > 1 ? std::atoi(argv[1]) : 4101;

    auto* scroll = new QScrollArea;
    auto* stack = new QWidget;
    auto* layout = new QVBoxLayout(stack);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    QVector<QWidget*> rows;
    rows.reserve(n);
    for (int i = 0; i < n; ++i) {
        auto* w = new QWidget(stack);
        w->setFixedHeight(24);
        rows.append(w);
        layout->addWidget(w);
    }
    scroll->setWidget(stack);
    scroll->resize(300, 600);
    scroll->show();
    app.processEvents();

    // Warm: one sweep to let Qt prime the update region machinery.
    for (QWidget* w : rows) w->update();
    app.processEvents();

    const int rep = 20;
    double best = 1e9, worst = 0, sum = 0;
    for (int r = 0; r < rep; ++r) {
        auto a = cnow::now();
        for (QWidget* w : rows) w->update();
        auto b = cnow::now();
        const double ms = std::chrono::duration<double, std::milli>(b - a).count();
        best = std::min(best, ms);
        worst = std::max(worst, ms);
        sum += ms;
    }
    std::printf("rows=%d update() sweep: avg %.3f ms  best %.3f  worst %.3f\n",
                n, sum / rep, best, worst);

    // Two-row variant (what a targeted update would cost).
    auto a = cnow::now();
    for (int r = 0; r < 2000; ++r) { rows[0]->update(); rows[1]->update(); }
    auto b = cnow::now();
    std::printf("2-row x2000: %.3f ms  (=> %.4f ms per pair)\n",
                std::chrono::duration<double, std::milli>(b - a).count(),
                std::chrono::duration<double, std::milli>(b - a).count() / 2000.0);
    return 0;
}