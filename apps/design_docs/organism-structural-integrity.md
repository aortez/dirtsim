# Organism Structural Integrity Design

## Overview

Organisms (trees, future creatures) are represented as **rigid bodies in continuous space** that project onto the discrete cell grid each frame. This approach treats organisms as proper physics objects rather than collections of cells with synchronized velocities.

## Problem with Previous Approach

The original design used **velocity synchronization**: all cells in an organism received the same velocity via flood-fill. This had a fundamental flaw:

**Cells have independent COMs (Centers of Mass)** that move based on velocity. Even with unified velocity, cells cross grid boundaries at different times because their COMs start at different positions within their cells. This causes the structure to tear apart during movement.

```
Organism moving right at velocity (0.5, 0):

[WOOD A]─[WOOD B]─[WOOD C]
com.x=0.8  com.x=0.2  com.x=-0.3

Frame 1: Cell A crosses boundary first (com reaches 1.0)
Frame 2: Cell B still inside its cell
Frame 3: Cell B crosses boundary
→ Structure tears apart!
```

The velocity sync approach is preserved below for historical reference but is **obsolete**.

---

## New Design: Organisms as Continuous-Space Rigid Bodies

### Core Concept

**Two representations:**
1. **Physics**: Organism lives in continuous space (position, velocity as Vector2d)
2. **Rendering**: Organism projects onto discrete grid each frame

```
Physics (continuous) → Collision Detection → Response → Grid Projection
         ↑                                                      ↓
         └──────────── Forces Gathered ─────────────────────────┘
```

### Phase 1: Position + Velocity (No Rotation)

#### Organism Structure

```cpp
struct Organism {
    OrganismId id;
    OrganismType type;  // TREE, DUCK, etc.

    // Continuous rigid body state
    Vector2d position;        // Anchor point (seed/root location)
    Vector2d velocity;        // Linear velocity (cells/sec)
    double mass;              // Total mass (computed from shape)
    Vector2d center_of_mass;  // Relative to position (computed)

    // Shape definition (local coordinates, axis-aligned)
    struct LocalCell {
        Vector2i local_pos;      // Position relative to anchor
        MaterialType material;
        double fill_ratio;
    };
    std::vector<LocalCell> local_shape;

    // Current grid projection (updated each frame)
    std::vector<Vector2i> occupied_cells;
};
```

#### Update Loop (Each Frame)

```cpp
void updateOrganism(Organism& org, Grid& grid, double dt) {
    // 1. Gather forces from environment
    Vector2d total_force = gatherEnvironmentForces(org, grid);

    // 2. Compute desired movement
    Vector2d acceleration = total_force / org.mass;
    Vector2d desired_velocity = org.velocity + acceleration * dt;
    Vector2d desired_position = org.position + desired_velocity * dt;

    // 3. Predict where cells would be
    std::vector<Vector2i> predicted_cells;
    for (const auto& local : org.local_shape) {
        Vector2d world = desired_position + Vector2d(local.local_pos);
        predicted_cells.push_back(snap(world));  // floor()
    }

    // 4. Detect collisions
    CollisionInfo collision = detectCollisions(predicted_cells, grid, org.id);

    // 5. Collision response (impulse-based)
    if (collision.blocked) {
        handleCollision_Impulse(org, collision, grid, dt);
    } else {
        org.position = desired_position;
        org.velocity = desired_velocity;
    }

    // 6. Project organism onto grid
    projectToGrid(org, grid);
}
```

### Collision Detection

```cpp
struct CollisionInfo {
    bool blocked;
    std::vector<Vector2i> blocked_cells;
    Vector2d contact_normal;  // Average surface normal
};

CollisionInfo detectCollisions(
    const std::vector<Vector2i>& target_cells,
    const Grid& grid,
    OrganismId org_id)
{
    CollisionInfo info{.blocked = false};
    Vector2d normal_sum = {0, 0};

    for (const auto& cell_pos : target_cells) {
        if (!isInBounds(cell_pos, grid)) {
            info.blocked = true;
            info.blocked_cells.push_back(cell_pos);
            normal_sum += computeBoundaryNormal(cell_pos);
            continue;
        }

        const Cell& cell = grid.at(cell_pos);

        // Blocked by wall
        if (cell.material_type == MaterialType::WALL) {
            info.blocked = true;
            info.blocked_cells.push_back(cell_pos);
            normal_sum += computeContactNormal(cell_pos, grid);
        }
        // Blocked by another organism
        else if (cell.organism_id != 0 && cell.organism_id != org_id) {
            info.blocked = true;
            info.blocked_cells.push_back(cell_pos);
            normal_sum += computeContactNormal(cell_pos, grid);
        }
        // Blocked by dense solid material
        else if (isSolidMaterial(cell) && cell.fill_ratio > 0.8) {
            info.blocked = true;
            info.blocked_cells.push_back(cell_pos);
            normal_sum += computeContactNormal(cell_pos, grid);
        }
    }

    if (info.blocked) {
        info.contact_normal = normal_sum.normalize();
    }

    return info;
}
```

### Collision Response (Impulse-Based)

Standard rigid body impulse calculation, same method used in Box2D/PhysX/Bullet:

```cpp
void handleCollision_Impulse(
    Organism& org,
    const CollisionInfo& collision,
    Grid& grid,
    double dt)
{
    Vector2d normal = collision.contact_normal;
    double v_normal = dot(org.velocity, normal);

    if (v_normal >= 0) return;  // Moving away from surface

    // Coefficient of restitution
    // 0.0 = perfectly inelastic (no bounce)
    // 1.0 = perfectly elastic (full bounce)
    double e = 0.3;

    // Impulse magnitude: j = -(1 + e) * v_n * m
    double j = -(1 + e) * v_normal * org.mass;
    Vector2d impulse = normal * j;

    // Apply to organism
    org.velocity += impulse / org.mass;

    // Apply reaction forces to environment (Newton's 3rd law)
    int num_contacts = collision.blocked_cells.size();
    Vector2d force_per_cell = -impulse / (dt * num_contacts);

    for (const auto& cell_pos : collision.blocked_cells) {
        Cell& cell = grid.at(cell_pos);

        if (cell.material_type == MaterialType::WALL) {
            continue;  // Walls are immovable
        }

        // Apply force uniformly - material properties handle response
        cell.pending_force += force_per_cell;
    }
}
```

### Reaction Forces (Newton's 3rd Law)

Organisms apply forces back to the environment. No material-specific heuristics - the existing physics system handles material differences through viscosity, friction, and pressure.

```cpp
void applyWeightToGround(Organism& org, Grid& grid) {
    double weight = org.mass * gravity;
    std::vector<Vector2i> bottom_cells = findGroundContactCells(org);

    if (bottom_cells.empty()) return;

    Vector2d force_per_cell = {0, weight / bottom_cells.size()};

    for (const auto& cell_pos : bottom_cells) {
        Vector2i ground_pos = cell_pos + Vector2i{0, 1};
        if (!isInBounds(ground_pos)) continue;

        Cell& ground_cell = grid.at(ground_pos);
        if (ground_cell.organism_id == org.id) continue;
        if (ground_cell.material_type == MaterialType::WALL) continue;

        ground_cell.pending_force += force_per_cell;
    }
}
```

### Grid Projection

```cpp
void projectToGrid(Organism& org, Grid& grid) {
    // Clear old projections
    for (const auto& old_pos : org.occupied_cells) {
        Cell& cell = grid.at(old_pos);
        if (cell.organism_id == org.id) {
            cell.organism_id = 0;
            cell.material_type = MaterialType::AIR;
            cell.fill_ratio = 0.0;
        }
    }

    org.occupied_cells.clear();

    // Project each local cell to grid
    for (const auto& local : org.local_shape) {
        Vector2d world_pos = org.position + Vector2d(local.local_pos);
        Vector2i grid_pos = snap(world_pos);  // floor()

        if (!isInBounds(grid_pos, grid)) continue;

        Cell& cell = grid.at(grid_pos);
        cell.organism_id = org.id;
        cell.material_type = local.material;
        cell.fill_ratio = local.fill_ratio;
        cell.velocity = org.velocity;

        // Compute sub-cell COM from fractional position
        Vector2d offset = world_pos - Vector2d(grid_pos);
        cell.com = offset * 2.0 - Vector2d{1, 1};  // Map [0,1] → [-1,1]

        org.occupied_cells.push_back(grid_pos);
    }
}
```

### Force Gathering

```cpp
Vector2d gatherEnvironmentForces(const Organism& org, const Grid& grid) {
    Vector2d total_force = {0, 0};

    for (const auto& local : org.local_shape) {
        Vector2d world_pos = org.position + Vector2d(local.local_pos);
        Vector2i grid_pos = snap(world_pos);

        if (!isInBounds(grid_pos, grid)) continue;

        total_force += grid.at(grid_pos).pending_force;
    }

    // Gravity
    total_force += org.mass * gravity_vector;

    return total_force;
}
```

### Growth

```cpp
void growCell(Organism& org, Vector2i local_pos, MaterialType mat) {
    org.local_shape.push_back({
        .local_pos = local_pos,
        .material = mat,
        .fill_ratio = 1.0
    });

    double new_mass = getMaterialProperties(mat).density;
    org.mass += new_mass;
    org.center_of_mass = recomputeCOM(org.local_shape);
    // Position (anchor) stays fixed, COM shifts relative to it
}
```

## Design Decisions

1. **Position = anchor point (not COM)**: Keeps coordinates stable during growth.
2. **No rotation in Phase 1**: Axis-aligned organisms only. Add rotation later.
3. **Impulse-based collision**: Industry standard, physically correct.
4. **No connectivity checks**: Removed for now. Can add back for fragmentation.
5. **Simple force gathering**: floor() for grid lookup, sufficient for phase 1.
6. **Uniform force application**: No material-specific heuristics.
7. **Grid-only serialization**: Client sees projected grid, unchanged.

## Phase 2: Future Enhancements

- **Rotation**: Add `rotation`, `angular_velocity`, `moment_of_inertia`.
- **Torque**: Compute from force positions relative to COM.
- **Stress-based fracture**: Track stress at bonds, break under load.
- **Organism-organism collision**: Two-body impulse resolution.

## Implementation Status

**Phase 1 - TODO:**
- [ ] Add continuous position/velocity to Organism struct
- [ ] Implement collision detection
- [ ] Implement impulse-based collision response
- [ ] Implement grid projection
- [ ] Apply reaction forces to environment
- [ ] Migrate Tree to new system
- [ ] Remove old velocity sync code

---

## Historical Reference: Velocity Sync Approach (OBSOLETE)

The following documents the original approach for reference. **Do not implement this** - it has fundamental flaws described above.

### Original Algorithm: Flood-Fill + Prune + Velocity Sync

1. **FLOOD FILL FROM SEED**
   - Start from tree.seed_position
   - Follow ROOT, WOOD, SEED cells (4-connected)
   - Build set of connected positions

2. **PRUNE DISCONNECTED FRAGMENTS**
   - Any ROOT/WOOD not in connected set loses organism_id
   - Becomes independent particle

3. **APPLY UNIFIED VELOCITY**
   - Gather pending_force from all connected cells
   - acceleration = total_force / total_mass
   - Set all connected cells to same velocity

### Why It Failed

- Unified velocity doesn't unify COM positions
- Each cell's COM still moves independently
- Boundary crossings happen at different times
- Structure tears apart during movement
- No mechanism for collision detection/response
- No reaction forces applied to environment
