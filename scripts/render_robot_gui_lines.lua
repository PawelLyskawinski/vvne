function script(command, layout, screen_width, screen_height, player_y_location_meters, camera_x_pitch_radians, camera_y_pitch_radians)

    local width               = 0.75
    local height              = 1.0
    local offset_up           = 0.20
    local ruler_lid_length    = 0.05
    local vertical_correction = 0.008
    local tiny_line_offset    = 0.011
    local max_left_x          = -0.5 * width
    local max_right_x         = -max_left_x
    local top_y               = -0.5 * height - offset_up
    local bottom_y            = 0.5 * height - offset_up

    local green  = {125.0 / 255.0, 204.0 / 255.0, 174.0 / 255.0, 0.9}
    local red    = {1.0, 0.0, 0.0, 0.9}
    local yellow = {1.0, 1.0, 0.0, 0.7}
    local blue   = {0.0, 0.0, 1.0, 0.9}

    function draw_offset(p, offset, c, w)
        p = { p[1], p[2], p[1] + offset[1], p[2] + offset[2]}
        line_draw(command, layout, p, c, w)
    end

    function draw(p, c, w)
        line_draw(command, layout, p, c, w)
    end

    function draw_scissor(p, s)
        draw_offset(p, {0.0, s[2]}, blue, 1.0)
        draw_offset(p, {s[1], 0.0}, blue, 1.0)
        draw_offset({p[1] + s[1], p[2]}, {0.0, s[2]}, blue, 1.0)
        draw_offset({p[1], p[2] + s[2]}, {s[1], 0.0}, blue, 1.0)
    end

    -- whole screen for now.. It'll need to be fine-tuned later on
    set_scissor(command, screen_width, screen_height, 0, 0)

    -- rulers at the end of vertical lines
    draw_offset({max_left_x,  top_y-0.005},     {ruler_lid_length, 0.0}, green, 5.0)
    draw_offset({max_left_x,  bottom_y+0.005},  {ruler_lid_length, 0.0}, green, 5.0)
    draw_offset({max_right_x, top_y-0.005},    {-ruler_lid_length, 0.0}, green, 5.0)
    draw_offset({max_right_x, bottom_y+0.005}, {-ruler_lid_length, 0.0}, green, 5.0)

    -- long vertical lines
    draw({max_left_x + ruler_lid_length, top_y - vertical_correction, max_left_x + ruler_lid_length, bottom_y + vertical_correction},
            green, 3.0)

    draw({max_right_x - ruler_lid_length, top_y - vertical_correction, max_right_x - ruler_lid_length, bottom_y + vertical_correction},
            green, 3.0)

    -- detail on left long vertical ruler
    for i=2,25 do

        local length = 0.0
        if 0 == ((i-1) + 2) % 5 then
            length = length + 0.01
        else
            length = length + 0.025
        end

        draw({
            max_left_x + length,
            top_y + ((i-1) * 0.04),
            max_left_x + ruler_lid_length - tiny_line_offset,
            top_y + ((i-1) * 0.04)
            },
            green, 3.0)
    end

    -- tiny vertical lines along the big green ones
    draw({max_left_x + ruler_lid_length - tiny_line_offset, top_y - vertical_correction,
         max_left_x + ruler_lid_length - tiny_line_offset, bottom_y + vertical_correction}, green, 2.0)

    draw({max_right_x - ruler_lid_length + tiny_line_offset, top_y - vertical_correction,
         max_right_x - ruler_lid_length + tiny_line_offset, bottom_y + vertical_correction}, green, 2.0)

    function speed_meter_with_frame()
        local length  = 0.125
        local upper_y = -0.202

        -- vertical
        draw_offset({max_left_x - 0.09 - (0.5 * length), upper_y + 0.000}, {length, 0.0}, green, 1.0)
        draw_offset({max_left_x - 0.09 - (0.5 * length), upper_y + 0.040}, {length, 0.0}, green, 1.0)
        draw_offset({max_left_x - 0.09 - (0.5 * length), upper_y + 0.065}, {length, 0.0}, green, 1.0)

        -- horizontal  asdad
        draw_offset({max_left_x - 0.09 - (0.5 * length), upper_y}, {0.0, 0.065}, green, 1.0)
        draw_offset({max_left_x - 0.09 + (0.5 * length), upper_y}, {0.0, 0.065}, green, 1.0)

        -- "SPEED" text inside speed meter frame
        local letter_left_x        = max_left_x - length
        local letter_bottom_y      = upper_y + 0.0595
        local letter_width         = 0.01
        local letter_height        = 0.014
        local letter_space_between = 0.005

        -- 'S'
        draw_offset({letter_left_x, letter_bottom_y}, {letter_width, 0.0}, green, 1.0)
        draw_offset({letter_left_x, letter_bottom_y - (0.5 * letter_height)}, {letter_width, 0.0}, green, 1.0)
        draw_offset({letter_left_x, letter_bottom_y - letter_height}, {letter_width, 0.0}, green, 1.0)
        draw_offset({letter_left_x + letter_width, letter_bottom_y}, {0.0, -(0.5 * letter_height)}, green, 1.0)
        draw_offset({letter_left_x, letter_bottom_y - (0.5 * letter_height)}, {0.0, -(0.5 * letter_height)}, green, 1.0)

        -- 'P'
        letter_left_x = letter_left_x + letter_width + letter_space_between;
        draw_offset({letter_left_x, letter_bottom_y}, {0.0, -letter_height}, green, 1.0)
        draw_offset({letter_left_x, letter_bottom_y - letter_height}, {letter_width, 0.0}, green, 1.0)
        draw_offset({letter_left_x + letter_width, letter_bottom_y - letter_height}, {0.0, 0.5 * letter_height}, green, 1.0)
        draw_offset({letter_left_x + letter_width, letter_bottom_y - (0.5 * letter_height)}, {-letter_width, 0.0}, green, 1.0)

        -- 'E' 2x
        for i=1,2 do
            letter_left_x = letter_left_x + letter_width + letter_space_between;
            draw_offset({letter_left_x, letter_bottom_y}, {0.0, -letter_height}, green, 1.0)
            draw_offset({letter_left_x, letter_bottom_y - letter_height}, {letter_width, 0.0}, green, 1.0)
            draw_offset({letter_left_x, letter_bottom_y - (0.5 * letter_height)}, {letter_width, 0.0}, green, 1.0)
            draw_offset({letter_left_x, letter_bottom_y}, {letter_width, 0.0}, green, 1.0)
        end

        -- 'D'
        letter_left_x = letter_left_x + letter_width + letter_space_between;
        draw_offset({letter_left_x, letter_bottom_y}, {0.0, -letter_height}, green, 1.0)
        draw_offset({letter_left_x, letter_bottom_y - letter_height}, {0.75 * letter_width, 0.0}, green, 1.0)
        draw_offset({letter_left_x, letter_bottom_y}, {0.75 * letter_width, 0.0}, green, 1.0)
        draw_offset({letter_left_x + (0.75 * letter_width), letter_bottom_y - letter_height}, {0.25 * letter_width, 0.25 * letter_height}, green, 1.0)
        draw_offset({letter_left_x + (0.75 * letter_width), letter_bottom_y}, {0.25 * letter_width, -(0.25 * letter_height)}, green, 1.0)
        draw_offset({letter_left_x + letter_width, letter_bottom_y - (0.25 * letter_height)}, {0.0, -(0.5 * letter_height)}, green, 1.0)

        -- "km/h" text inside speed meter frame
        letter_left_x              = max_left_x + 0.04 - length
        letter_bottom_y            = upper_y + 0.033
        letter_width               = 0.01
        letter_height              = 0.025
        letter_space_between       = 0.003
        local letter_y_guide = letter_bottom_y - (0.6 * letter_height)

        -- 'K'
        draw_offset({letter_left_x, letter_bottom_y}, {0.0, -letter_height}, green, 1.0)
        draw_offset({letter_left_x, letter_bottom_y - (0.2 * letter_height)}, {letter_width, -(0.4 * letter_height)}, green, 1.0)
        draw({letter_left_x + (0.5 * letter_width), letter_bottom_y - (0.35 * letter_height), letter_left_x + letter_width, letter_bottom_y}, green, 1.0)

        -- 'M'
        letter_left_x = letter_left_x + letter_width + letter_space_between;
        draw_offset({letter_left_x, letter_y_guide}, {letter_width, 0.0}, green, 1.0)
        draw({letter_left_x, letter_bottom_y, letter_left_x, letter_y_guide}, green, 1.0)
        draw({letter_left_x + (0.5 * letter_width), letter_bottom_y, letter_left_x + (0.5 * letter_width), letter_y_guide}, green, 1.0)
        draw({letter_left_x + letter_width, letter_bottom_y, letter_left_x + letter_width, letter_y_guide}, green, 1.0)

        -- slash
        letter_left_x = letter_left_x + letter_width + letter_space_between;
        draw({letter_left_x, letter_bottom_y, letter_left_x + letter_width, letter_bottom_y - letter_height}, green, 1.0)

        -- 'H'
        letter_left_x = letter_left_x + letter_width + letter_space_between;
        draw({letter_left_x, letter_bottom_y, letter_left_x, letter_bottom_y - letter_height}, green, 1.0)
        draw({letter_left_x, letter_y_guide, letter_left_x + letter_width, letter_y_guide}, green, 1.0)
        draw({letter_left_x + letter_width, letter_bottom_y, letter_left_x + letter_width, letter_y_guide}, green, 1.0)
    end

    speed_meter_with_frame()

    function compass_border()
        local compass_width   = 0.5
        local compass_height  = 0.04
        local bottom_y_offset = 0.38

        draw_offset({-0.5 * compass_width, bottom_y_offset}, {compass_width, 0.0}, green, 1.0)
        draw_offset({-0.5 * compass_width, bottom_y_offset - compass_height}, {compass_width, 0.0}, green, 1.0)
        draw_offset({-0.5 * compass_width, bottom_y_offset}, {0.0, -compass_height}, green, 1.0)
        draw_offset({0.5 * compass_width, bottom_y_offset}, {0.0, -compass_height}, green, 1.0)
    end

    compass_border()

    function abs(input)
        if 0.0 > input then
            input = -input
        end
        return input
    end

    function adjust_coord(input) -- converts screen coord (-1.0, 1.0) to pixel coord
        if 0.0 > input then
            input = 1.0 + input
        end
        return 0.5 * input
    end

    function scissor_wrap(w, h, x_off, y_off) -- input as screen coords
        draw_scissor({x_off, y_off}, {w, h})
        set_scissor(command,
                screen_width * adjust_coord(w),
                screen_height * adjust_coord(h),
                screen_width * adjust_coord(x_off),
                screen_height * adjust_coord(y_off))
    end

    -- scissor_wrap(width, height, max_left_x, top_y)

    --
    function height_rulers()
        local red_x_offset                 = 0.02
        local height_ruler_length          = 0.04
        local height_ruler_left_x_position = max_left_x + ruler_lid_length + red_x_offset

        for side=1,2 do

            if 2 == side then
                -- scissor_wrap(height_ruler_length, height, height_ruler_left_x_position, top_y) aaa
                -- scissor_wrap(height_ruler_length, height, height_ruler_left_x_position + 0.573, top_y)
            else
                -- scissor_wrap(2.0, 2.0, -0.99, -0.99)
                scissor_wrap(height_ruler_length, height, height_ruler_left_x_position, top_y)
                -- scissor_wrap(height_ruler_length, height, height_ruler_left_x_position + 0.55, top_y)
            end
            --

            for i=1,5 do
                local side_mod = 0.0
                if 1 == side then
                    side_mod = -1.0
                else
                    side_mod = 1.0
                end

                local base_offset = {side_mod * height_ruler_left_x_position, player_y_location_meters / 8.0}

                -- endless repetition
                while -0.5 < base_offset[2] do
                    base_offset[2] = base_offset[2] - 0.8
                end

                local size = {side_mod * height_ruler_length, 0.2};

                local offset = {
                    base_offset[1],
                    base_offset[2] + 0.4 * (i-1)
                }

                draw_offset({offset[1], offset[2] + (0.5 * size[2])}, {size[1], 0.0}, red, 1.0)
                draw_offset({offset[1], offset[2] + (0.5 * size[2])}, {0.0, -size[2]}, red, 1.0)
                draw_offset({offset[1], offset[2] - (0.5 * size[2])}, {size[1], 0.0}, red, 1.0)
            end
        end
    end

    height_rulers()
end
