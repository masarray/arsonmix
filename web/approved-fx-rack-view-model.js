// SPDX-License-Identifier: AGPL-3.0-only
// GUI-2: pure FX Rack readback projection for the ONE approved /preview/ GUI.
// No DOM, timer, native dispatch, plugin lifecycle or accepted audio authority.
// Only the already-validated native snapshot may supply these values.
(function (root, factory) {
  const api=factory();
  if(typeof module==="object"&&module.exports)module.exports=api;
  if(root)root.ArSonApprovedFxRackViewModel=api;
})(typeof globalThis==="object"?globalThis:undefined,function () {
  "use strict";

  // null means missing or incomplete readback, never a guess based on CSS or
  // an optimistic click. Channel-local, global and insert bypass are distinct.
  function powerReadback(channel,globalFxBypassed,channelName) {
    if(typeof channel?.fxRackBypassed!=="boolean" ||
       !Array.isArray(channel.inserts) ||
       typeof globalFxBypassed!=="boolean")return null;
    const enabled=!channel.fxRackBypassed,globalEnabled=!globalFxBypassed;
    const name=String(channelName??"");
    const slots=[];
    for(let index=0;index<channel.inserts.length;index++) {
      const insert=channel.inserts[index];
      if(!insert || typeof insert.id!=="string" || !insert.id ||
         typeof insert.bypassed!=="boolean")return null;
      const effective=enabled&&globalEnabled&&!insert.bypassed;
      const action=insert.bypassed?"Enable":"Bypass";
      const latencyTitle=insert.latencyChanged===true?
        "Plugin delay changed · aligned dry signal retained; reload the plugin before using its new delay":
        Number.isInteger(insert.latencySamples)?
          "Plugin delay: "+insert.latencySamples+" samples · compensated":null;
      slots.push({
        index,insertId:insert.id,bypassed:insert.bypassed,effective,
        latencyTitle,
        pressed:String(effective),
        title:action+" only insert "+(index+1),
        label:action+" only FX insert "+(index+1)+" in "+name
      });
    }
    return {
      enabled,globalEnabled,chainBypassed:!enabled||!globalEnabled,
      headerPressed:String(enabled&&globalEnabled),
      headerTitle:(enabled?"Bypass":"Enable")+" all channel FX",
      headerLabel:(enabled?"Bypass":"Enable")+" entire FX chain in "+name,
      slots
    };
  }

  // A mounted strip belongs to a specific native FX instance and order.
  // Check the FULL roster before the bridge writes any slot or button.
  // An absent/duplicate/wrong slot or missing power control forces the
  // existing authoritative structural re-projection instead of wrong-FX UI.
  function mountedRackMatches(readback,mountedSlots,hasHeader) {
    if(!readback || !hasHeader || !Array.isArray(mountedSlots) ||
       mountedSlots.length!==readback.slots.length)return false;
    const byIndex=new Map();
    for(const mounted of mountedSlots) {
      if(!Number.isInteger(mounted?.index) ||
         byIndex.has(mounted.index))return false;
      byIndex.set(mounted.index,mounted);
    }
    return readback.slots.every(slot=>{
      const mounted=byIndex.get(slot.index);
      return mounted?.insertId===slot.insertId && mounted?.hasPower===true;
    });
  }

  return Object.freeze({powerReadback,mountedRackMatches});
});
