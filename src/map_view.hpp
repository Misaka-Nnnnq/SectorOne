#pragma once

#include <memory>

class MapView {
public:
    MapView();
    ~MapView();

    MapView(const MapView&) = delete;
    MapView& operator=(const MapView&) = delete;

    void draw();

private:
    class Impl;
    std::unique_ptr<Impl> impl;
};
