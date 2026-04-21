contract C {
    uint public x;
    function set(uint _x) public {
        x = _x;
    }
    function get() public view returns (uint) {
        return x;
    }
}
// ----
// C.creation.instructions[0].context.code.range.offset: 62
// C.creation.instructions[0].context.code.range.length: 162
// C.creation.instructions[0].context.code.source.id: 0
//
// C.runtime.instructions[0].context.code:
// {
//     "range": {
//         "length": 162,
//         "offset": 62
//     },
//     "source": {
//         "id": 0
//     }
// }
