#pragma once

#include <diagnostics/readiness.hpp>
#include <foundation/latest_job.hpp>
#include <thread>

namespace app
{

class map_loader
{
public:
    explicit map_loader(diagnostics::readiness::channel& geometry);
    ~map_loader();
    void request(std::string map);

private:
    void run(std::stop_token stop);
    // Publish the geometry readiness state that matches what is currently installed in the collision world.
    void publish_geometry(std::uint64_t serial, bool from_file);
    foundation::latest_job_queue m_queue;
    diagnostics::readiness::channel& m_geometry;
    std::jthread m_worker;
};

} // namespace app
