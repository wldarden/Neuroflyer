1. Hidden layer count should be controllable when creating a new genome.
2. A genome should auto create its initial variant, which is just a randomized-weights version of itself. I think the next error is because no variant exists, but i cant even create a variant.
3. I created a genome, opened the genome, and tried clicking "Train fresh", then got this error: "libc++abi: terminating due to uncaught exception of type std::invalid_argument: Input size mismatch: expected 35 got 36
   zsh: abort      ./build/neuroflyer/neuroflyer"
4. I cant "view neural net" in the "Inspection" section while viewing a genome. got error: "Failed to load variant for viewing: Cannot open file for reading: neuroflyer/data/genomes/Alpha/Alpha.bin"
5. 




1. 
2. Fitness Function screen is drawing the ship but we dont need the ship on that page. the ships is therefore covering all the inputs.
3. we need to change the save for training. We should have a "Finish Training" Button that shows the user another page:
   - The page should list the Elite individuals from the most recently completed generation on the left. columns: name, score
   - The right side of the page should show the neural network of the hovered elite row in the list. 
   - clicking an elite row should highlight it, meaning its "one of the selected"
   - Below the elite list should be a "Save Selected" button that saves all the selected elites as variants, and then takes the user to the genome page where they can see the variants they just saved added to the variants list.




1. vision system should be removed from the hangar page. this will now be controlled from the Neural net viewer.


1. hangar view should have a list on the left with large rows for each Genome. 
   - Each row should display [a small preview of its body, Name, Variant count, Last Updated date] 
   - Hovering a Row Should show that Genomes Neural Net on the right side of the page.
   - The top row should always be the "Create Genome" row. on click, take the user to a "Create Genome Page"
   - Clicking on a genome brings the user to the "Genome Page" (What we see when we click "Open Genome" currently)
   - Genome page should not show the ship.
   - Create Genome Page:
      - The top center of the page should be the Name input field in large/scifi text/font. 
      - There will be an input/control pane down the left side. It has these sections in this order: ["Frame" (body select and vision type select), "Node Types" (inputs for n sight/sensor/mem nodes), "Network Layers" (layer count, layer node count inputs)]
      - The Neural Net should be displayed on the right.
      - The displayed neural net should change/respond to changing the inputs for number of layers/node count/memory nodes/sight rays/sensor rays.
   

   


1. Have a full width section beneath the genome name when on the Genome Page that shows the variant list and selected variant controls. show the default variant for this genome.
