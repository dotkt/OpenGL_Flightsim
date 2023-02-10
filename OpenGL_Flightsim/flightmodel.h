#pragma once

#include "phi.h"
#include "gfx.h"
#include "data.h"

#include <cmath>
#include <vector>
#include <tuple>
#include <algorithm>

struct Airfoil
{
    float min, max;
    std::vector<ValueTuple> data;

    Airfoil(const std::vector<ValueTuple> &curve_data) : data(curve_data)
    {
        min = curve_data[0].alpha;
        max = curve_data[curve_data.size() - 1].alpha;
    }

    std::tuple<float, float> sample(float alpha) const
    {
        int index = static_cast<int>(phi::utils::scale(alpha, min, max, 0, data.size()));
        index = phi::utils::clamp(index, 0, static_cast<int>(data.size() - 1U));
        if (!(0 <= index && index < data.size()))
        {
            printf("alpha = %f, index = %d, size = %d\n", alpha, index, (int)data.size());
            assert(false);
        }
        return { data[index].cl, data[index].cd };
    }
};

Airfoil NACA_0012(NACA_0012_data);
Airfoil NACA_2412(NACA_2412_data);

struct Engine
{   
    float throttle = 0.5f;    
    float thrust = 10000.0f; 
    float horsepower = 1000.0f;
    float rpm = 2400.0f;

    Engine(float engine_thrust) : thrust(engine_thrust) {}
    
    void apply_forces(phi::RigidBody &rigid_body)
    {
        rigid_body.add_relative_force({ thrust * throttle, 0.0f, 0.0f });
        // TODO: implement torque from propeller
    }
};

struct Wing
{
    const float area{};
    const glm::vec3 position{};
    const Airfoil *airfoil;

    glm::vec3 normal;
    float lift_multiplier = 1.0f;
    float drag_multiplier = 1.0f;
    phi::Degrees deflection = 0.0f;
    
    Wing(const glm::vec3 &position, float area, const Airfoil *aero, const glm::vec3 &normal = phi::UP)
         : position(position),
          area(area),
          airfoil(aero),
          normal(normal)
    {}

    Wing(const glm::vec3& position, float wingspan, float chord, const Airfoil* aero, const glm::vec3& normal = phi::UP)
        : position(position),
        area(chord * wingspan),
        airfoil(aero),
        normal(normal)
    {}
    
    void apply_forces(phi::RigidBody &rigid_body)
    {
        auto local_velocity = rigid_body.get_point_velocity(position);
        auto speed = glm::length(local_velocity);

        if (speed <= 0.0f || area <= 0.0f)
            return;

        auto wing_normal = normal;

        if (abs(deflection) > phi::epsilon)
        {
            auto axis = glm::normalize(glm::cross(phi::FORWARD, normal));
            glm::mat4 rot(1.0f);
            rot = glm::rotate(rot, glm::radians(deflection), axis);
            wing_normal = glm::vec3(rot * glm::vec4(normal, 1.0f));
        }

        auto drag_direction = glm::normalize(-local_velocity);
        auto lift_direction 
            = glm::normalize(glm::cross(glm::cross(drag_direction, wing_normal), drag_direction));

        auto angle_of_attack = glm::degrees(std::asin(glm::dot(drag_direction, wing_normal)));

        auto [lift_coefficient, drag_coefficient] = airfoil->sample(angle_of_attack);

        float tmp = phi::sq(speed) * phi::rho * area * 0.5f;
        auto lift = lift_direction * lift_coefficient * lift_multiplier * tmp;
        auto drag = drag_direction * drag_coefficient * drag_multiplier * tmp;

        rigid_body.add_force_at_point(lift + drag, position);
    }
};

struct Aircraft
{
    Engine engine;
    std::vector<Wing> elements;
    phi::RigidBody rigid_body;
    glm::vec3 joystick{}; // roll, yaw, pitch

    float log_timer = 1.0f;

    Aircraft(float mass, float thrust, glm::mat3 inertia, std::vector<Wing> wings)
        : elements(wings), rigid_body({ .mass = mass, .inertia = inertia }), engine(thrust)
    {}

    void update(phi::Seconds dt)
    {
#if 1
        Wing& la = elements[1];
        Wing& ra = elements[2];
        Wing& el = elements[4];
        Wing& ru = elements[5];

        float roll = joystick.x;
        float yaw = joystick.y;
        float pitch = joystick.z;
        float max_elevator_deflection = 5.0f, max_aileron_deflection = 15.0f, max_rudder_deflection = 5.0f;
        float aileron_deflection = roll * max_aileron_deflection;
        la.deflection = +aileron_deflection;
        ra.deflection = -aileron_deflection;
        el.deflection = -(pitch * max_elevator_deflection);
        ru.deflection = yaw * max_rudder_deflection;
#else
        const glm::vec3 control_torque = { 1500000.0f, 1000.0f, 1000000.0f };
        float control_authority = phi::utils::clamp(glm::length(rigid_body.velocity) / 150.0f, 0.0f, 1.0f);
        rigid_body.add_relative_torque((joystick * control_torque) * control_authority);
#endif

        for (Wing& wing : elements)
        {
            wing.apply_forces(rigid_body);
        }    

        engine.apply_forces(rigid_body);

        if ((log_timer += dt) > 0.5f)
        {
            log_timer = 0;
#if 1
            printf(
                "%.2f km/h, thr: %.2f, alt: %.2f m\n", 
                phi::units::kilometer_per_hour(glm::length(rigid_body.velocity)),
                engine.throttle,
                rigid_body.position.y
            );
#endif
        }

        rigid_body.update(dt);
    }
};
