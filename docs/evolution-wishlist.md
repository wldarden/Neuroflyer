# Wishlist of Evolution Features in Neuroflyer

## 1 Flyer Parameters

---

### Sensors

- [ ] **Category**: Occulus, Raycast
- [ ] **is_Sensor**: "Sensor" vs "Sight". "Sensor" means the sensor has all the multihot encoding nodes, "Sight" means the sensor only has the distance node.
- [ ] **Range**: Maximum distance the sensor can detect
- [ ] **Width**: The width of Occulus sensors
- [ ] **count**: Number of sensors flyer has
- [ ] **angle**: The angle at which the sensors are placed on a flyer (e.g., directly forward, 45 degrees, etc.)
- [ ] **noise**: The amount of noise in the sensor readings (e.g., 0.1 for 10% noise)
- [ ] **refresh_rate**: How often the sensors update their readings (e.g., the sensor input only changes every x ticks)

### Attributes

- [ ] **speed**: How many units the flyer moves each tick (would only be interesting if speed scaled fitness points distance earns. High speed would reduce distance points, slow speed would increase distance points)
- [ ] **body_type**: the ship graphic used to represent the flyer (e.g., triangle, circle, square, gif, etc.). this should be configurable behavior: like "enable/disable ship body chaning" so the user can force them to use a particular body type. 
Or let user link body type used to some parameter such that ships in the sim use different body types based on that; like "body_type = speed" so faster ships use one body type and slower ships use another. Or body_type based on evolutionary distance of neural network, or MRCA, 
such that the sim view body type would indicate subspecies within the run.
### Neural Net Nodes:

- [ ] **memory_nodes**: Number of nodes dedicated to memory (i.e., storing past sensor readings or actions)
- [ ] **node_types**: CRTNN, LSTM, etc.
- [ ] **activation_functions**: ReLU, sigmoid, etc.

### Fitness Function Parameters

- ???

### Mutation Parameters
For a genome, be able to configure whether these parameters are allowed to evolve or set.
- [ ] **mutation_rate**: The probability to mutate during reproduction (e.g., 0.1 for 10% chance of mutation)
- [ ] **mutation_strength**: How much a parameter changes when it mutates (e.g., 0.5 for a 50% change)
- [ ] **crossover_rate**: The probability to crossover during reproduction 
- [ ] **selection_pressure**: How strongly the selection process favors fitter individuals (e.g., tournament size in tournament selection)
- [ ] **elitism_rate**: The percentage of top performers that are guaranteed to survive to the next generation (e.g., 0.05 for top 5%)


## 2 Lineage Tree Viewer
Might be part of an in-progress claude activity already: "neuroflyer/docs/superpowers/plans/2026-03-24-save-system.md".
   
    - A) a page that displays a provided lineage tree.
    - B) Variants, aka individuals are shown as nodes
    - C) Connections indicate anscestry links. 
    - D) Nodes are double clickable to open a sub page that displays the variant's neural network. 
    - E) Devise a method of calculating "Evolutionary Distance" between nodes by processing their neural network 
        parameters in some way. Add ability to optionally color code lineage tree by this "Distance" value such that 
        "far away" individuals are more different in color than they are to "near" ones. 
    - F) Brainstorm and implement several colorization modes: color by sensor composition, evolutionary distance, 
        node count, layer count, etc. for the lineage tree. 
    - G) Ability to "Create Genome from Node", "Create Variant From Node", "Delete Node", etc. 
    - H) Click nodes to view stats about the variant represented by the node. 
    - I) Lineage tree "Independent". We are going to use this on the sim pause screen, in the hangar view, id love to use
        it for other projects entirely. so having it be more like an evolution package optional feature add on or 
        something would be ideal. 

## 3 Evolution Settings Management
A screen to manage the evolution parameters. Most likely a settings link on the main page? should genomes save unique evo params?
or should they be app wide?

    - A) a page that displays all the evolution parameters in an organized way. 
    - B) Enable/Disable mutation methods, such as crossover, tournament size, mutation rate/jump, etc. 

## 4 Manual Variant Recombination

    - A) Ability to select 2 variants and sexually reproduce them, or to asexully reproduce 1 selected variant. 
    - B) Once child is made, ability to explore its neural network/parameters, and then choose wether to save it or discard it.



