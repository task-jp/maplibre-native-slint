#include <iostream>
#include <memory>
#include <set>
#include <vector>

#include "map_window.h"
#include "slint_maplibre_headless.hpp"

int main(int argc, char** argv) {
    std::cout << "[main] Starting application" << std::endl;
    auto main_window = MapWindow::create();
    auto slint_map_libre = std::make_shared<SlintMapLibre>();

    // Delay initialization until a non-zero window size is known
    auto initialized = std::make_shared<bool>(false);
    const auto size = main_window->get_window_size();
    std::cout << "Initial Window Size: " << static_cast<int>(size.width) << "x"
              << static_cast<int>(size.height) << std::endl;

    // This function is ONLY for rendering. It will be called by the observer.
    auto render_function = [=]() {
        std::cout << "Rendering map..." << std::endl;
        auto image = slint_map_libre->render_map();
        main_window->global<MapAdapter>().set_map_texture(image);
    };

    // Pass the render function to SlintMapLibre, which will pass it to the
    // observer.
    slint_map_libre->setRenderCallback(render_function);

    // Set up icon registration callback - will be called when style loads
    slint_map_libre->setIconRegistrationCallback([=]() {
        std::cout << "[main] Icon registration callback invoked" << std::endl;

        // Collect unique icon names from POI data
        auto pois_model = main_window->get_pois();
        std::set<std::string> icon_names;

        for (size_t i = 0; i < pois_model->row_count(); ++i) {
            auto poi = pois_model->row_data(i);
            if (!poi.has_value()) continue;

            // Only collect icons for POIs with marker-type == Icon
            if (poi->marker_type == MarkerType::Icon && !poi->icon.empty()) {
                icon_names.insert(std::string(poi->icon.data(), poi->icon.size()));
            }
        }

        // Register each unique icon
        for (const auto& icon_name : icon_names) {
            std::cout << "[main] Registering icon: " << icon_name << std::endl;
            // Note: The slint::Image parameter is not used by register_poi_icon,
            // which loads the PNG file directly from the filesystem
            main_window->global<MapAdapter>().invoke_register_poi_icon(
                slint::SharedString(icon_name), slint::Image());
        }
    });

    // The timer in .slint file will trigger this callback periodically.
    // This callback drives the MapLibre run loop and, if needed, performs
    // rendering on the UI thread.
    main_window->global<MapAdapter>().on_tick_map_loop([=]() {
        slint_map_libre->run_map_loop();
        if (slint_map_libre->take_repaint_request() ||
            slint_map_libre->consume_forced_repaint()) {
            render_function();
        }
    });

    main_window->global<MapAdapter>().on_style_changed(
        [=](const slint::SharedString& url) {
            slint_map_libre->setStyleUrl(std::string(url.data(), url.size()));
        });

    // Connect mouse events
    main_window->global<MapAdapter>().on_mouse_press(
        [=](float x, float y) { slint_map_libre->handle_mouse_press(x, y); });

    main_window->global<MapAdapter>().on_mouse_release(
        [=](float x, float y) { slint_map_libre->handle_mouse_release(x, y); });

    main_window->global<MapAdapter>().on_mouse_move(
        [=](float x, float y, bool pressed) {
            slint_map_libre->handle_mouse_move(x, y, pressed);
        });

    // Double click zoom with Shift for zoom-out
    main_window->global<MapAdapter>().on_double_click_with_shift(
        [=](float x, float y, bool shift) {
            slint_map_libre->handle_double_click(x, y, shift);
        });

    // Wheel zoom
    main_window->global<MapAdapter>().on_wheel_zoom(
        [=](float x, float y, float dy) {
            slint_map_libre->handle_wheel_zoom(x, y, dy);
        });

    // Pitch and bearing controls
    main_window->global<MapAdapter>().on_pitch_changed(
        [=](int pitch_value) { slint_map_libre->set_pitch(pitch_value); });

    main_window->global<MapAdapter>().on_bearing_changed(
        [=](float bearing_value) {
            slint_map_libre->set_bearing(bearing_value);
        });

    main_window->global<MapAdapter>().on_fly_to(
        [=](const slint::SharedString& location) {
            slint_map_libre->fly_to(
                std::string(location.data(), location.size()));
        });

    // Register POI icon callback
    main_window->global<MapAdapter>().on_register_poi_icon(
        [=](const slint::SharedString& icon_id, const slint::Image& image) {
            slint_map_libre->register_poi_icon(
                std::string(icon_id.data(), icon_id.size()), image);
        });

    // POI updates - convert POI array to GeoJSON
    main_window->global<MapAdapter>().on_pois_updated(
        [=](const std::shared_ptr<slint::Model<POI>>& pois_model) {
            if (!pois_model) {
                std::cout << "[main] Received null POI model" << std::endl;
                return;
            }

            size_t count = pois_model->row_count();
            std::cout << "[main] Received " << count << " POIs" << std::endl;

            // Build GeoJSON FeatureCollection
            std::string geojson = R"({"type":"FeatureCollection","features":[)";
            for (size_t i = 0; i < count; ++i) {
                auto poi = pois_model->row_data(i);
                if (!poi.has_value())
                    continue;

                if (i > 0)
                    geojson += ",";

                geojson += R"({"type":"Feature","geometry":{"type":"Point","coordinates":[)";
                geojson += std::to_string(poi->longitude);
                geojson += ",";
                geojson += std::to_string(poi->latitude);
                geojson += R"(]},"properties":{"name":")";
                geojson += std::string(poi->name.data(), poi->name.size());
                geojson += R"(","marker-type":")";
                // Convert enum to string using Slint-generated enum
                switch (poi->marker_type) {
                    case MarkerType::Icon:
                        geojson += "icon";
                        break;
                    case MarkerType::Circle:
                        geojson += "circle";
                        break;
                    case MarkerType::Text:
                        geojson += "text";
                        break;
                }
                geojson += R"(","icon":")";
                geojson += std::string(poi->icon.data(), poi->icon.size());
                geojson += R"(","pitch-alignment":")";
                // Convert pitch alignment enum to string
                switch (poi->pitch_alignment) {
                    case PitchAlignment::Map:
                        geojson += "map";
                        break;
                    case PitchAlignment::Viewport:
                        geojson += "viewport";
                        break;
                }
                geojson += R"("}})";;
            }
            geojson += "]}";

            std::cout << "[main] GeoJSON: " << geojson << std::endl;
            slint_map_libre->update_pois_geojson(geojson);
        });

    // Initialize/resize MapLibre to match the map image area
    main_window->on_map_size_changed([=]() {
        const auto s = main_window->get_map_size();
        const int w = static_cast<int>(s.width);
        const int h = static_cast<int>(s.height);
        std::cout << "Map Area Size Changed: " << w << "x" << h << std::endl;
        if (w > 0 && h > 0) {
            if (!*initialized) {
                slint_map_libre->initialize(w, h);
                *initialized = true;

                // Trigger initial POI update by manually calling the callback
                // with current POI data
                main_window->global<MapAdapter>().invoke_pois_updated(
                    main_window->get_pois());
            } else {
                slint_map_libre->resize(w, h);
            }
        }
    });

    std::cout << "[main] Entering UI event loop" << std::endl;
    try {
        main_window->run();
        std::cout << "[main] UI event loop exited normally" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "[main] Unhandled exception: " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "[main] Unhandled unknown exception" << std::endl;
    }
    return 1;
}
