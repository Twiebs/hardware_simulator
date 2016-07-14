struct Toolbar {
  uint32_t nodeTypes[10];
  uint32_t hotkey[10]; 
  size_t count;
};

enum EditorMode {
  EditorMode_None,
  EditorMode_SelectBox,
  EditorMode_DEFAULT,
  EditorMode_PLACEMENT,
};

struct Editor {
  DynamicArray<EditorNode *> inputs;
  DynamicArray<EditorNode *> nodes;
  DynamicArray<NodeIndex> selectedNodes;
  DynamicArray<ICDefinition *> icdefs;

  ImVec2 viewPosition;

  Toolbar toolbar;
  EditorMode mode;

  uint32_t placementNodeType;
  ImVec2 selectBoxOrigin;
};

void DrawToolbarVerticaly(Toolbar *t, Editor *e){
  const uint8_t *keystate = SDL_GetKeyboardState(NULL);

  ImGui::BeginGroup();
  for(size_t i = 0; i < t->count; i++){
    const char *buttonText = "X"; 
    if(t->nodeTypes[i] != NodeType_INVALID){
      assert(t->nodeTypes[i] < NodeType_COUNT);
      buttonText = NodeName[t->nodeTypes[i]];
    }

    static const ImColor DEFAULT_COLOR = ImColor(50, 50, 50); 
    static const ImColor ACTIVE_COLOR = ImColor(180, 140, 50);
    const ImColor color = (t->nodeTypes[i] == e->placementNodeType && e->mode == EditorMode_PLACEMENT) ? ACTIVE_COLOR : DEFAULT_COLOR;
    ImGui::PushStyleColor(ImGuiCol_Button, color);
    if(ImGui::Button(buttonText, ImVec2(32, 32)) || keystate[t->hotkey[i]] == 1){
      e->placementNodeType = t->nodeTypes[i];
      e->mode = EditorMode_PLACEMENT;
    }
    ImGui::PopStyleColor();


  }
  ImGui::EndGroup();
}

