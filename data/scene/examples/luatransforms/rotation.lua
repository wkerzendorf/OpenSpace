-- t1:  Ingame seconds past the J2000 epoch
-- t2:  Wallclock milliseconds past the J2000 epoch

function rotation(t1, t2)
    return 1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0
end