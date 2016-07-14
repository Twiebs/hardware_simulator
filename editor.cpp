struct Toolbar {
  uint32_t nodeTypes[10];
  uint32_t keycode[10]; 
  size_t count;
};

enum EditorMode {
  EditorMode_None,
  EditorMode_SelectBox,
  EditorMode_DEFAULT,
};

struct Editor {
  DynamicArray<EditorNode *> inputs;
  DynamicArray<EditorNode *> nodes;
  DynamicArray<NodeIndex> selectedNodes;
  DynamicArray<ICDefinition *> icdefs;

  ImVec2 viewPosition;

  Toolbar toolbar;
  EditorMode mode;

  union {

    struct /*EditorMode_CREATE*/{
      uint32_t placementNodeType;
    };

    ImVec2 selectBoxOrigin;
  };
};

void DrawToolbarVerticaly(Toolbar *t, Editor *e){
  ImGui::BeginGroup();
  for(size_t i = 0; i < t->count; i++){
    const char *buttonText = "X"; 
    if(t->nodeTypes[i] != NodeType_INVALID){
      assert(t->nodeTypes[i] < NodeType_COUNT);
      buttonText = NodeName[t->nodeTypes[i]];
    }

    if(ImGui::Button(buttonText, ImVec2(32, 32))){
      e->placementNodeType = t->nodeTypes[i];
      e->mode = EditorMode_DEFAULT;
    }

  }
  ImGui::EndGroup();
}

