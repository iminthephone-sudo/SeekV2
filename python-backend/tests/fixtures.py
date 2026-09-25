"""Shared sample data: a realistic reentry participant and two job pages."""

SAMPLE_PROFILE = {
    "name": "Warehouse focus",
    "participant": "Marcus R.",
    "contact": {"full_name": "Marcus Reed", "email": "marcus.reed@example.org", "phone": "(555) 201-3344",
                "city": "Tacoma", "state": "WA"},
    "headline": "Warehouse & Kitchen Crew Lead",
    "summary": "Dependable crew lead with four years of food service and material handling experience. "
               "Forklift and ServSafe certified, known for training new team members and keeping work areas safe.",
    "skills": ["Forklift", "ServSafe", "teamwork", "Inventory"],
    "experience": [
        {"title": "Kitchen Crew Lead", "employer": "Washington State Correctional Facility", "location": "Monroe, WA",
         "start": "2019", "end": "2023",
         "bullets": ["Responsible for inmates in the kitchen",
                     "Prepared 1,200 meals daily while meeting food safety and kitchen sanitation standards",
                     "Trained 15 new crew members on knife skills and food preparation",
                     "Tracked inventory of dry goods and reduced waste by 10%"]},
        {"title": "Warehouse Associate", "employer": "Northwest Supply Co.", "location": "Kent, WA",
         "start": "Mar 2024", "end": "Present",
         "bullets": ["Operated forklift and pallet jack to move 150+ pallets per shift with zero incidents",
                     "Picked and packed customer orders using an RF scanner",
                     "Loaded and unloaded freight trucks at the dock"]},
    ],
    "certifications": [{"name": "Forklift Certification", "issuer": "OSHA-authorized trainer", "date": "2023"},
                       {"name": "ServSafe Food Handler", "issuer": "National Restaurant Association", "date": "2022"}],
    "training": [{"name": "Pre-Apprenticeship Construction Program", "provider": "Trades Pathways", "hours": "240",
                  "date": "2023"}],
    "education": [{"credential": "GED", "school": "Edmonds College", "end": "2021"}],
}

JOB_TEXT = """Warehouse Associate — FastShip Logistics
FastShip is a fair chance employer and welcomes applicants with criminal records.

Responsibilities:
• Operate forklift and pallet jack to move freight safely
• Pick and pack customer orders using an RF scanner
• Perform cycle counting and maintain inventory accuracy
• Load and unload trucks at the dock

Requirements:
• Must have forklift certification
• Must be able to lift 50 lbs repeatedly
• OSHA 10 preferred
• Experience with a warehouse management system (WMS) is a plus
• Strong attention to detail and teamwork
"""

JOB_HTML_JSONLD = """<!doctype html><html><head><title>Warehouse Associate | FastShip Careers</title>
<script type="application/ld+json">
{"@context":"https://schema.org","@type":"JobPosting","title":"Warehouse Associate",
 "hiringOrganization":{"@type":"Organization","name":"FastShip Logistics"},
 "jobLocation":{"@type":"Place","address":{"addressLocality":"Tacoma","addressRegion":"WA"}},
 "employmentType":"FULL_TIME",
 "baseSalary":{"@type":"MonetaryAmount","currency":"USD","value":{"@type":"QuantitativeValue","minValue":21,"maxValue":24,"unitText":"HOUR"}},
 "datePosted":"2026-09-01",
 "description":"<p>FastShip is a <b>fair chance employer</b>.</p><h3>Responsibilities</h3><ul><li>Operate forklift and pallet jack to move freight safely</li><li>Pick and pack customer orders using an RF scanner</li><li>Perform cycle counting and maintain inventory accuracy</li></ul><h3>Requirements</h3><ul><li>Must have forklift certification</li><li>OSHA 10 preferred</li><li>Strong attention to detail and teamwork</li><li>Background check required</li></ul>"}
</script></head><body><h1>Warehouse Associate</h1></body></html>"""

JOB_HTML_PLAIN = """<!doctype html><html><head><title>Line Cook - Harbor Grill</title>
<meta property="og:site_name" content="Harbor Grill"></head><body>
<nav><a href="/">Home</a><a href="/menu">Menu</a><a href="/jobs">Jobs</a></nav>
<main><h1>Line Cook</h1><div class="posting">
<p>Harbor Grill is hiring a Line Cook to join our busy kitchen team in Tacoma.</p>
<h2>What you'll do</h2><ul><li>Prepare food to recipe standards on the grill and fry stations</li>
<li>Maintain food safety and kitchen sanitation</li><li>Stock and rotate inventory</li></ul>
<h2>Qualifications</h2><ul><li>Food handler's card required</li><li>ServSafe preferred</li>
<li>Knife skills and ability to work in a fast-paced environment</li><li>Teamwork and reliability</li></ul>
</div></main><footer>© Harbor Grill</footer></body></html>"""
